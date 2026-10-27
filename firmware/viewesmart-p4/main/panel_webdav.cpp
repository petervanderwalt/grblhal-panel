#include "panel_webdav.h"
#include "panel_runtime.h"
#include "panel_storage.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include <algorithm>
#include <atomic>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
namespace panel {
namespace {
constexpr const char *Root="/sdcard", *Work="/sdcard/.panel-webdav",
    *Temp="/sdcard/.panel-webdav/upload", *Backup="/sdcard/.panel-webdav/backup",
    *Journal="/sdcard/.panel-webdav/journal";
constexpr size_t MaxFile=512u*1024*1024;
constexpr const char *Methods="OPTIONS, PROPFIND, PROPPATCH, GET, HEAD, PUT, DELETE, MKCOL, MOVE, COPY, LOCK, UNLOCK";
static std::atomic<bool> enabled{false};
static uint32_t tag_epoch,tag_revision;
static portMUX_TYPE activity_lock=portMUX_INITIALIZER_UNLOCKED;
static WebdavActivity activity;
static uint32_t ticks() { return esp_timer_get_time()/1000; }
static void progress(uint32_t done,uint32_t total) {
    uint32_t stamp=ticks();
    portENTER_CRITICAL(&activity_lock); activity.done=done; activity.total=total; activity.updated=stamp; portEXIT_CRITICAL(&activity_lock);
}
static void begin_activity(const char *action,const char *path) {
    WebdavActivity next;next.active=true;next.started=next.updated=ticks();
    snprintf(next.action,sizeof next.action,"%s",action);snprintf(next.path,sizeof next.path,"%s",path);
    portENTER_CRITICAL(&activity_lock);activity=next;portEXIT_CRITICAL(&activity_lock);
}
struct Access {
    bool acquired=runtime_file_access_begin(), changed=false, success=false, visible=false;
    void show(const char *action,const char *path) { visible=true; begin_activity(action,path); }
    ~Access() {
        if (!acquired) return;
        if(changed) ++tag_revision;
        storage_remote_end(changed);
        if (visible) {
            uint32_t stamp=ticks();
            portENTER_CRITICAL(&activity_lock); activity.active=false; activity.failed=!success; activity.updated=stamp; portEXIT_CRITICAL(&activity_lock);
        }
    }
};
// FAT/OEM and the LCD catalogue share printable ASCII paths. Reject FAT aliases too.
static bool path(const char *uri,char *out,size_t capacity) {
    if (strncmp(uri,"/sd",3) || (uri[3] && uri[3]!='/')) return false;
    size_t n=0;
    const char *p=uri+3;
    if (!*p) p="/";
    while (*p) {
        unsigned char c=*p++;
        if (c=='%') {
            if (!isxdigit((unsigned char)p[0]) || !p[0] || !p[1] || !isxdigit((unsigned char)p[1])) return false;
            auto hex=[](char h) {return h<='9'?h-'0':tolower((unsigned char)h)-'a'+10;};
            c=hex(p[0])*16+hex(p[1]); p+=2;
            if (c=='/' || c=='\\') return false;
        }
        if (c<32 || c>=127 || strchr("\\:*?\"<>|#%",c) || n+1>=capacity) return false;
        out[n++]=c;
    }
    while(n>1 && out[n-1]=='/') --n;
    out[n]=0;
    for (const char *part=out+1;*part;) {
        const char *end=strchr(part,'/'); if (!end) end=part+strlen(part);
        size_t count=end-part;
        if (!count || part[count-1]==' ' || part[count-1]=='.' ||
            (count>=6 && !strncasecmp(part,".panel",6))) return false;
        part=*end?end+1:end;
    }
    return n && out[0]=='/';
}
static void full_path(const char *relative,char *full,size_t size) { snprintf(full,size,"%s%s",Root,relative); }
static bool info(const char *p,struct stat &s) { return !stat(p,&s); }
static bool remove_tree(const char *p,unsigned depth=0) {
    struct stat s;
    if (!info(p,s)) return errno==ENOENT;
    if (!S_ISDIR(s.st_mode)) return unlink(p)==0;
    if (depth>=16) return false;
    DIR *dir=opendir(p); if(!dir)return false;
    char *child=(char *)heap_caps_malloc(384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool ok=child;
    while(ok) {
        auto e=readdir(dir);if(!e)break;
        if(!strcmp(e->d_name,".") || !strcmp(e->d_name,".."))continue;
        if(snprintf(child,384,"%s/%s",p,e->d_name)>=384 || !remove_tree(child,depth+1)) {ok=false;break;}
    }
    free(child);closedir(dir); return ok && rmdir(p)==0;
}
static bool write_all(int fd,const char *data,size_t size) {
    while(size) { ssize_t n=write(fd,data,size); if(n<=0)return false; data+=n;size-=n; } return true;
}
static bool copy_tree(const char *source,const char *target,char *buffer,size_t capacity,uint32_t &used,uint32_t total,unsigned depth=0) {
    struct stat s;if(!info(source,s) || depth>8)return false;
    if(!S_ISDIR(s.st_mode)) {
        if(s.st_size<0 || (uint64_t)s.st_size>MaxFile-used)return false;
        FILE *in=fopen(source,"rb");int out=open(target,O_WRONLY|O_CREAT|O_EXCL,0666);bool ok=in && out>=0;
        size_t remaining=s.st_size;
        while(ok && remaining){size_t n=fread(buffer,1,std::min(capacity,remaining),in);if(!n || !write_all(out,buffer,n)){ok=false;break;}remaining-=n;used+=n;progress(used,total);}
        if(in)fclose(in);
        if(out>=0){if(fsync(out))ok=false;if(close(out))ok=false;}return ok;
    }
    if(mkdir(target,0775))return false;
    DIR *dir=opendir(source);if(!dir)return false;
    struct Paths {char source[224],target[224];};
    auto paths=(Paths *)heap_caps_malloc(sizeof(Paths),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool ok=paths;
    while(ok) {
        auto e=readdir(dir);if(!e)break;
        if(!strcmp(e->d_name,".") || !strcmp(e->d_name,"..") || !strncasecmp(e->d_name,".panel",6))continue;
        if(snprintf(paths->source,sizeof paths->source,"%s/%s",source,e->d_name)>=(int)sizeof paths->source ||
           snprintf(paths->target,sizeof paths->target,"%s/%s",target,e->d_name)>=(int)sizeof paths->target) {ok=false;break;}
        ok=copy_tree(paths->source,paths->target,buffer,capacity,used,total,depth+1);
    }
    free(paths);closedir(dir);return ok;
}
static bool commit(const char *source,const char *relative,bool replacing) {
    char target[224];full_path(relative,target,sizeof target);
    int fd=open(Journal,O_WRONLY|O_CREAT|O_TRUNC,0666); if(fd<0)return false;
    bool ok=write_all(fd,relative,strlen(relative)) && fsync(fd)==0; if(close(fd))ok=false;
    if(!ok) {unlink(Journal);return false;}
    if(replacing && rename(target,Backup)) {unlink(Journal);return false;}
    if(rename(source,target)) {
        if(replacing && rename(Backup,target)) return false; // Keep the journal for boot recovery.
        unlink(Journal);return false;
    }
    // The replacement is committed; a retained journal retries housekeeping on restart.
    if(remove_tree(Backup))unlink(Journal);
    return true;
}
static esp_err_t reply(httpd_req_t *r,const char *status,const char *message="") {
    ESP_LOGI("webdav","%s %s -> %s",http_method_str((http_method)r->method),r->uri,status);
    httpd_resp_set_status(r,status);httpd_resp_set_type(r,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"DAV","1, 2");
    httpd_resp_set_hdr(r,"Allow",Methods);
    bool close=status[0]=='4' || status[0]=='5';
    if(close)httpd_resp_set_hdr(r,"Connection","close");
    esp_err_t result=httpd_resp_sendstr(r,message);return close?ESP_FAIL:result;
}
static bool header(httpd_req_t *r,const char *name,char *out,size_t size) {
    *out=0;size_t n=httpd_req_get_hdr_value_len(r,name);
    return !n || (n<size && httpd_req_get_hdr_value_str(r,name,out,size)==ESP_OK);
}
static void etag(const struct stat &s,char *out,size_t size) {
    snprintf(out,size,"\"%llx-%llx-%lx-%lx\"",(unsigned long long)s.st_size,(unsigned long long)s.st_mtime,(unsigned long)tag_epoch,(unsigned long)tag_revision);
}
#include "panel_webdav_locks.inc"
static bool conditions(httpd_req_t *r,bool exists,const struct stat &s) {
    char tag[64],value[160];etag(s,tag,sizeof tag);
    if(!header(r,"If-Match",value,sizeof value))return false;
    if(*value && (!exists || (strcmp(value,"*") && strcmp(value,tag))))return false;
    if(!header(r,"If-None-Match",value,sizeof value))return false;
    if(*value && exists && (!strcmp(value,"*") || !strcmp(value,tag)))return false;
    return true;
}
struct Xml {
    httpd_req_t *r; char buffer[768]={};size_t used=0;bool ok=true;
    void flush() { if(used && ok)ok=httpd_resp_send_chunk(r,buffer,used)==ESP_OK;used=0; }
    void text(const char *s) { while(*s && ok) {buffer[used++]=*s++;if(used==sizeof buffer)flush();} }
    void escaped(const char *s) {for(;*s;s++)switch(*s){case '&':text("&amp;");break;case '<':text("&lt;");break;case '>':text("&gt;");break;case '"':text("&quot;");break;default:char c[]={*s,0};text(c);}}
    void finish() {flush();if(ok)ok=httpd_resp_send_chunk(r,nullptr,0)==ESP_OK;}
};
static void href(Xml &xml,const char *relative,bool directory);
static void lease_xml(Xml &xml,const Lease &l) {
    xml.text("<D:activelock><D:locktype><D:write/></D:locktype><D:lockscope><D:exclusive/></D:lockscope><D:depth>");
    xml.text(l.deep?"infinity":"0");xml.text("</D:depth><D:timeout>");char timeout[40];snprintf(timeout,sizeof timeout,"Second-%u",(unsigned)std::max<int32_t>(1,(int32_t)(l.expires-ticks())/1000));xml.text(timeout);
    xml.text("</D:timeout><D:locktoken><D:href>");xml.escaped(l.token);xml.text("</D:href></D:locktoken><D:lockroot><D:href>");href(xml,l.path,false);xml.text("</D:href></D:lockroot></D:activelock>");
}
static esp_err_t lock_response(httpd_req_t *r,const Lease &l,bool created,bool refreshed=false) {
    char token[80];snprintf(token,sizeof token,"<%s>",l.token);
    httpd_resp_set_status(r,created?"201 Created":"200 OK");httpd_resp_set_type(r,"application/xml; charset=utf-8");httpd_resp_set_hdr(r,"DAV","1, 2");
    if(!refreshed)httpd_resp_set_hdr(r,"Lock-Token",token);
    Xml xml{r};xml.text("<?xml version=\"1.0\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery>");lease_xml(xml,l);xml.text("</D:lockdiscovery></D:prop>");xml.finish();return xml.ok?ESP_OK:ESP_FAIL;
}
static bool xml_element(const char *body,const char *name) {
    for(const char *p=body;(p=strchr(p,'<'));p++) {
        if(p[1]=='/' || p[1]=='?' || p[1]=='!')continue;
        const char *end=p+1;while(isalnum((unsigned char)*end)||*end==':'||*end=='_'||*end=='-')end++;
        const char *local=p+1;for(const char *q=local;q<end;q++)if(*q==':')local=q+1;
        if(size_t(end-local)==strlen(name) && !strncmp(local,name,end-local))return true;
    }
    return false;
}
static esp_err_t lock_resource(httpd_req_t *r,const char *relative,const char *full,bool exists,const struct stat &st,unsigned tokens,Access &access) {
    unsigned seconds=lease_seconds(r);if(!seconds)return reply(r,"400 Bad Request");
    if(!r->content_len) {
        if(leases)for(unsigned i=0;i<LeaseCount;i++)if((tokens&(1u<<i)) && !strcasecmp(relative,leases[i].path)){leases[i].expires=ticks()+seconds*1000;return lock_response(r,leases[i],false,true);}
        return reply(r,"412 Precondition Failed");
    }
    if(r->content_len>2048)return reply(r,"413 Payload Too Large");
    char *body=(char *)heap_caps_malloc(r->content_len+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!body)return reply(r,"503 Service Unavailable");
    size_t received=0;char stage[256];bool ok=true;
    while(received<r->content_len){int n=httpd_req_recv(r,stage,std::min(sizeof stage,r->content_len-received));if(n<=0){ok=false;break;}for(int i=0;i<n;i++)((volatile char *)body)[received++]=stage[i];}
    body[received]=0;ok=ok && strlen(body)==received && !strstr(body,"<!") && xml_element(body,"lockinfo") && xml_element(body,"exclusive") && xml_element(body,"write") && !xml_element(body,"shared");free(body);
    if(!ok)return reply(r,"400 Bad Request","Only exclusive write locks are supported");
    char depth[16];if(!header(r,"Depth",depth,sizeof depth) || (*depth && strcmp(depth,"0") && strcmp(depth,"infinity")))return reply(r,"400 Bad Request");
    bool deep=exists && S_ISDIR(st.st_mode) && strcmp(depth,"0");
    if(locked(relative,0,deep))return reply(r,"423 Locked");
    int slot=-1;if(leases)for(unsigned i=0;i<LeaseCount;i++)if(!*leases[i].path){slot=i;break;}
    if(slot<0)return reply(r,"507 Insufficient Storage","All write leases are in use");
    if(!exists){int file=open(full,O_WRONLY|O_CREAT|O_EXCL,0666);if(file<0)return reply(r,"409 Conflict");if(close(file))return reply(r,"500 Internal Server Error");access.changed=true;}
    auto &l=leases[slot];snprintf(l.path,sizeof l.path,"%s",relative);
    uint32_t random[4];esp_fill_random(random,sizeof random);
    snprintf(l.token,sizeof l.token,"opaquelocktoken:%08lx-%04lx-%04lx-%04lx-%04lx%08lx",(unsigned long)random[0],(unsigned long)(random[1]>>16),(unsigned long)((random[1]&0xfff)|0x4000),(unsigned long)(((random[2]>>16)&0x3fff)|0x8000),(unsigned long)(random[2]&0xffff),(unsigned long)random[3]);
    l.expires=ticks()+seconds*1000;l.deep=deep;return lock_response(r,l,!exists);
}
static esp_err_t unlock_resource(httpd_req_t *r,const char *relative) {
    char value[80];if(!header(r,"Lock-Token",value,sizeof value) || *value!='<' || strlen(value)<3 || value[strlen(value)-1]!='>')return reply(r,"400 Bad Request");
    value[strlen(value)-1]=0;expire_leases();
    if(leases)for(unsigned i=0;i<LeaseCount;i++)if(!strcasecmp(relative,leases[i].path) && !strcmp(value+1,leases[i].token)){leases[i].path[0]=0;return reply(r,"204 No Content");}
    return reply(r,"409 Conflict");
}
static const char *Dav="DAV:", *Microsoft="urn:schemas-microsoft-com:";
struct Property { char name[64]={}, ns[128]={}; };
struct Properties { Property items[24];unsigned count=0;bool all=true,names=false; };
static bool xml_name(char c) {return isalnum((unsigned char)c)||strchr("_.-:",c);}
static bool namespace_uri(const char *body,const char *prefix,char *out,size_t size) {
    char attr[80];snprintf(attr,sizeof attr,"xmlns%s%s",*prefix?":":"",prefix);
    for(const char *p=body;(p=strstr(p,attr));p++) {
        if(p!=body && !isspace((unsigned char)p[-1]))continue;
        const char *v=p+strlen(attr);while(isspace((unsigned char)*v))v++;
        if(*v++!='=')continue;
        while(isspace((unsigned char)*v))v++;
        char quote=*v++;if(quote!='\'' && quote!='"')return false;
        const char *end=strchr(v,quote);if(!end || size_t(end-v)>=size)return false;
        snprintf(out,size,"%.*s",int(end-v),v);return !strchr(out,'&');
    }
    *out=0;return !*prefix;
}
static bool parse_properties(const char *body,Properties &properties,bool patch) {
    if(!*body)return !patch;
    if(strstr(body,"<!"))return false; // No DTD, entity expansion or external XML resources.
    bool inside=false,mode_seen=false;unsigned nesting=0,prop_level=0;char open_names[16][64];
    for(const char *p=body;(p=strchr(p,'<'));p++) {
        if(p[1]=='?') {const char *end=strstr(p,"?>");if(!end)return false;p=end+1;continue;}
        const char *end=strchr(p,'>');if(!end)return false;
        bool closing=p[1]=='/',empty=end>p && end[-1]=='/';
        const char *start=p+(closing?2:1),*n=start;while(*n && xml_name(*n))n++;
        if(n==start || n-start>=64 || (!isalpha((unsigned char)*start) && *start!='_'))return false;
        char name[64],prefix[64]={};snprintf(name,sizeof name,"%.*s",int(n-start),start);
        char *local=strchr(name,':');
        if(local){snprintf(prefix,sizeof prefix,"%.*s",int(local-name),name);local++;}else local=name;
        if(!*local || strchr(local,':') || (!isalpha((unsigned char)*local) && *local!='_'))return false;
        if(closing) {if(!nesting || strcmp(open_names[nesting-1],name))return false;if(inside && nesting==prop_level)inside=false;nesting--;}
        else {
            if(nesting>=16)return false;
            snprintf(open_names[nesting++],64,"%s",name);
            if(!strcmp(local,"allprop")){if(patch || mode_seen)return false;properties.all=true;mode_seen=true;}
            if(!strcmp(local,"propname")){if(patch || mode_seen)return false;properties.names=true;mode_seen=true;}
            if(!strcmp(local,"prop")){if(mode_seen)return false;mode_seen=true;inside=true;prop_level=nesting;properties.all=false;}
            else if(inside && nesting==prop_level+1) {
                if(properties.count>=24)return false;
                auto &item=properties.items[properties.count++];
                snprintf(item.name,sizeof item.name,"%s",local);
                if(!namespace_uri(body,prefix,item.ns,sizeof item.ns))return false;
            }
            if(empty) {if(inside && nesting==prop_level)inside=false;nesting--;}
        }
        p=end;
    }
    return mode_seen && (properties.all || properties.count>0) && nesting==0;
}
static bool known(const Property &p) {
    const char *names[]={"displayname","resourcetype","getcontentlength","getlastmodified","creationdate","getetag","getcontenttype","supportedlock","lockdiscovery",nullptr};
    if(!strcmp(p.ns,Dav)){for(auto n:names)if(n && !strcmp(p.name,n))return true;}
    return !strcmp(p.ns,Microsoft) && !strcmp(p.name,"Win32FileAttributes");
}
static void property(Xml &xml,const Property &p,const struct stat &s,const char *name,bool value,const char *relative) {
    xml.text(*p.ns?"<P:":"<");xml.text(p.name);xml.text(*p.ns?" xmlns:P=\"":" xmlns=\"");xml.escaped(p.ns);xml.text("\">");
    char text[80];bool directory=S_ISDIR(s.st_mode);struct tm date;
    if(value && known(p)) {
        if(!strcmp(p.name,"resourcetype")){if(directory)xml.text("<D:collection/>");}
        else if(!strcmp(p.name,"displayname"))xml.escaped(*name?name:"SD card");
        else if(!strcmp(p.name,"getcontentlength")){snprintf(text,sizeof text,"%llu",directory?0ull:(unsigned long long)s.st_size);xml.text(text);}
        else if(!strcmp(p.name,"getetag")){etag(s,text,sizeof text);xml.escaped(text);}
        else if(!strcmp(p.name,"getlastmodified")){gmtime_r(&s.st_mtime,&date);strftime(text,sizeof text,"%a, %d %b %Y %H:%M:%S GMT",&date);xml.text(text);}
        else if(!strcmp(p.name,"creationdate")){gmtime_r(&s.st_mtime,&date);strftime(text,sizeof text,"%Y-%m-%dT%H:%M:%SZ",&date);xml.text(text);}
        else if(!strcmp(p.name,"getcontenttype"))xml.text(directory?"httpd/unix-directory":"application/octet-stream");
        else if(!strcmp(p.name,"Win32FileAttributes"))xml.text(directory?"00000010":"00000020");
        else if(!strcmp(p.name,"supportedlock"))xml.text("<D:lockentry><D:lockscope><D:exclusive/></D:lockscope><D:locktype><D:write/></D:locktype></D:lockentry>");
        else if(!strcmp(p.name,"lockdiscovery")){expire_leases();if(leases)for(unsigned i=0;i<LeaseCount;i++)if(*leases[i].path && covers(leases[i],relative))lease_xml(xml,leases[i]);}
    }
    xml.text(*p.ns?"</P:":"</");xml.text(p.name);xml.text(">");
}
static void href(Xml &xml,const char *relative,bool directory) {
    xml.text("/sd");
    for(const unsigned char *p=(const unsigned char *)relative;*p;p++) {
        if(isalnum(*p)||strchr("/-_.~",*p)){char c[]={(char)*p,0};xml.text(c);}
        else {char escape[4];snprintf(escape,sizeof escape,"%%%02X",*p);xml.text(escape);}
    }
    if(directory && strcmp(relative,"/"))xml.text("/");
}
static void resource(Xml &xml,const char *relative,const struct stat &s,const Properties &props,bool patch,bool virtual_root=false) {
    const char *name=strrchr(relative,'/');name=name?name+1:relative;
    xml.text("<D:response><D:href>");if(virtual_root)xml.text("/");else href(xml,relative,S_ISDIR(s.st_mode));xml.text("</D:href>");
    const char *defaults[]={"displayname","resourcetype","getcontentlength","getlastmodified","creationdate","getetag","getcontenttype","supportedlock","lockdiscovery"};
    for(int group=0;group<2;group++) {
        bool any=props.all ? group==0 : false;
        if(!props.all)for(unsigned i=0;i<props.count;i++)any|=(known(props.items[i])?0:1)==group;
        if(!any)continue;
        xml.text("<D:propstat><D:prop>");
        if(props.all)for(auto name:defaults){Property p;snprintf(p.name,sizeof p.name,"%s",name);snprintf(p.ns,sizeof p.ns,"DAV:");property(xml,p,s,strrchr(relative,'/')+1,!props.names,relative);}
        else for(unsigned i=0;i<props.count;i++)if((known(props.items[i])?0:1)==group)property(xml,props.items[i],s,name,!props.names && !patch && group==0,relative);
        xml.text("</D:prop><D:status>");xml.text(patch?"HTTP/1.1 403 Forbidden":group?"HTTP/1.1 404 Not Found":"HTTP/1.1 200 OK");xml.text("</D:status></D:propstat>");
    }
    xml.text("</D:response>");
}
static esp_err_t properties(httpd_req_t *r,const char *relative,const char *full,const struct stat &s,bool patch,bool virtual_root=false) {
    char depth[16];if(!header(r,"Depth",depth,sizeof depth))return reply(r,"400 Bad Request");
    if(!patch && strcmp(depth,"0") && strcmp(depth,"1"))return reply(r,"403 Forbidden","Use Depth: 0 or 1");
    if(r->content_len>4096)return reply(r,"413 Payload Too Large");
    auto props=(Properties *)heap_caps_calloc(1,sizeof(Properties),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    char *body=(char *)heap_caps_malloc(r->content_len+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!props || !body){free(props);free(body);return reply(r,"503 Service Unavailable");}
    props->all=true;size_t used=0;char buffer[512];bool ok=true;
    while(used<r->content_len){int n=httpd_req_recv(r,buffer,std::min(sizeof buffer,r->content_len-used));if(n<=0){ok=false;break;}for(int i=0;i<n;i++)((volatile char *)body)[used++]=buffer[i];}
    body[used]=0;ok=ok && strlen(body)==used && parse_properties(body,*props,patch);free(body);
    if(!ok){free(props);return reply(r,"400 Bad Request","Invalid property request");}
    httpd_resp_set_status(r,"207 Multi-Status");httpd_resp_set_type(r,"application/xml; charset=utf-8");httpd_resp_set_hdr(r,"DAV","1, 2");
    Xml xml{r};xml.text("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\">");resource(xml,relative,s,*props,patch,virtual_root);
    if(virtual_root && strcmp(depth,"0"))resource(xml,"/",s,*props,false);
    else if(!patch && S_ISDIR(s.st_mode) && strcmp(depth,"0")) {
        DIR *dir=opendir(full);
        if(dir) {
            while(xml.ok) {
                auto e=readdir(dir);if(!e)break;
                if(!strcmp(e->d_name,".") || !strcmp(e->d_name,"..") || !strncasecmp(e->d_name,".panel",6))continue;
                char child[160],uri[164],fullchild[224];
                if(snprintf(uri,sizeof uri,"/sd%s%s%s",relative,!strcmp(relative,"/")?"":"/",e->d_name)>=(int)sizeof uri || !path(uri,child,sizeof child))continue;
                full_path(child,fullchild,sizeof fullchild);struct stat entry;if(info(fullchild,entry))resource(xml,child,entry,*props,false);
            }
            closedir(dir);
        } else xml.ok=false;
    }
    xml.text("</D:multistatus>");xml.finish();free(props);return xml.ok?ESP_OK:ESP_FAIL;
}
static bool raw_send(httpd_req_t *r,const char *bytes,size_t count) {
    while(count){int n=httpd_send(r,bytes,count);if(n<=0)return false;bytes+=n;count-=n;}return true;
}
static esp_err_t get(httpd_req_t *r,const char *full,const char *relative,const struct stat &s,Access &access) {
    if(S_ISDIR(s.st_mode))return reply(r,"200 OK",r->method==HTTP_HEAD?"":"LCD SD card WebDAV collection");
    uint64_t first=0,last=s.st_size?s.st_size-1:0;char range[96],tag[64];bool partial=false;
    if(!header(r,"Range",range,sizeof range))return reply(r,"400 Bad Request");
    char if_range[96];if(!header(r,"If-Range",if_range,sizeof if_range))return reply(r,"400 Bad Request");
    etag(s,tag,sizeof tag);if(*if_range && strcmp(if_range,tag))*range=0;
    if(*range) {
        char *end=nullptr;
        if(strncmp(range,"bytes=",6) || strchr(range,',' ) || !s.st_size)return reply(r,"416 Range Not Satisfiable");
        const char *start=range+6;
        if(*start=='-'){unsigned long long suffix=strtoull(start+1,&end,10);if(end==start+1 || *end || !suffix)return reply(r,"416 Range Not Satisfiable");first=suffix<(uint64_t)s.st_size?s.st_size-suffix:0;}
        else {first=strtoull(start,&end,10);if(end==start || *end!='-')return reply(r,"416 Range Not Satisfiable");start=end+1;if(*start){last=strtoull(start,&end,10);if(end==start || *end)return reply(r,"416 Range Not Satisfiable");} }
        if(first>last || first>=(uint64_t)s.st_size)return reply(r,"416 Range Not Satisfiable");
        last=std::min<uint64_t>(last,s.st_size-1);partial=true;
    }
    FILE *file=fopen(full,"rb");if(!file)return reply(r,"500 Internal Server Error");
    if(first && fseek(file,first,SEEK_SET)){fclose(file);return reply(r,"500 Internal Server Error");}
    uint64_t total=s.st_size?last-first+1:0;etag(s,tag,sizeof tag);
    char headers[512],content_range[100]={};
    if(partial)snprintf(content_range,sizeof content_range,"Content-Range: bytes %llu-%llu/%llu\r\n",(unsigned long long)first,(unsigned long long)last,(unsigned long long)s.st_size);
    int n=snprintf(headers,sizeof headers,"HTTP/1.1 %s\r\nContent-Length: %llu\r\nContent-Type: application/octet-stream\r\nETag: %s\r\nAccept-Ranges: bytes\r\nDAV: 1, 2\r\nConnection: close\r\n%s\r\n",partial?"206 Partial Content":"200 OK",(unsigned long long)total,tag,content_range);
    bool ok=raw_send(r,headers,n);uint64_t sent=0;
    if(r->method!=HTTP_HEAD) {
        access.show("Downloading",relative);progress(0,total);
        auto buffer=(char *)heap_caps_aligned_alloc(64,1024,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
        if(!buffer)ok=false;
        while(ok && sent<total){size_t count=fread(buffer,1,std::min<uint64_t>(1024,total-sent),file);if(!count){ok=false;break;}ok=raw_send(r,buffer,count);sent+=count;progress(sent,total);}
        free(buffer);
    }
    fclose(file);access.success=ok;
    return ESP_FAIL; // Content-Length response is complete; close this file-transfer connection.
}
static esp_err_t handle(httpd_req_t *r) {
    ESP_LOGI("webdav","%s %s (%u bytes)",http_method_str((http_method)r->method),r->uri,(unsigned)r->content_len);
    if(!enabled)return reply(r,"403 Forbidden","Enable SD card sharing in Settings");
    if(httpd_req_get_hdr_value_len(r,"Origin"))return reply(r,"403 Forbidden","Use a WebDAV client");
    if(r->method==HTTP_OPTIONS)return reply(r,"200 OK");
    bool virtual_root=!strcmp(r->uri,"/");
    if(virtual_root && r->method!=HTTP_PROPFIND && r->method!=HTTP_GET && r->method!=HTTP_HEAD)return reply(r,"403 Forbidden");
    char relative[160],full[224];
    if(virtual_root)snprintf(relative,sizeof relative,"/");
    else if(!path(r->uri,relative,sizeof relative))return reply(r,"403 Forbidden","Invalid or private SD path");
    full_path(relative,full,sizeof full);
    if(r->method==HTTP_UNLOCK)return unlock_resource(r,relative);
    bool supported=r->method==HTTP_PROPFIND || r->method==HTTP_PROPPATCH || r->method==HTTP_GET || r->method==HTTP_HEAD || r->method==HTTP_PUT || r->method==HTTP_DELETE || r->method==HTTP_MKCOL || r->method==HTTP_MOVE || r->method==HTTP_COPY || r->method==HTTP_LOCK;
    if(!supported)return reply(r,"405 Method Not Allowed");
    Access access;if(!access.acquired)return reply(r,"423 Locked","SD card is busy; wait for the job, preview or transfer to finish");
    struct stat s={};bool exists=info(full,s);
    unsigned tokens=0;if(!if_conditions(r,relative,tokens))return reply(r,"412 Precondition Failed");
    if(exists && (r->method==HTTP_GET || r->method==HTTP_HEAD)) {
        char none[160],tag[64];etag(s,tag,sizeof tag);
        if(header(r,"If-None-Match",none,sizeof none) && (!strcmp(none,"*") || !strcmp(none,tag))){httpd_resp_set_hdr(r,"ETag",tag);return reply(r,"304 Not Modified");}
    }
    if(!conditions(r,exists,s))return reply(r,"412 Precondition Failed");
    if(r->content_len) {
        char expect[32];if(!header(r,"Expect",expect,sizeof expect) || (*expect && strcasecmp(expect,"100-continue")))return reply(r,"417 Expectation Failed");
        if(*expect){const char *interim="HTTP/1.1 100 Continue\r\n\r\n";if(httpd_send(r,interim,strlen(interim))!=(int)strlen(interim))return ESP_FAIL;}
    }
    if(r->method==HTTP_LOCK)return lock_resource(r,relative,full,exists,s,tokens,access);
    if(r->method==HTTP_PROPFIND || r->method==HTTP_PROPPATCH)return exists?properties(r,relative,full,s,r->method==HTTP_PROPPATCH,virtual_root):reply(r,"404 Not Found");
    if(r->method==HTTP_GET || r->method==HTTP_HEAD)return exists?get(r,full,relative,s,access):reply(r,"404 Not Found");
    if(!strcmp(relative,"/"))return reply(r,"403 Forbidden","The SD root cannot be changed");
    if(r->method!=HTTP_COPY && locked(relative,tokens,r->method==HTTP_MOVE || r->method==HTTP_DELETE))return reply(r,"423 Locked","A client holds a write lease");
    if((r->method==HTTP_DELETE || r->method==HTTP_MOVE || ((r->method==HTTP_PUT || r->method==HTTP_MKCOL) && !exists)) && parent_locked(relative,tokens))return reply(r,"423 Locked");
    if(r->method==HTTP_PUT) {
        if(exists && S_ISDIR(s.st_mode))return reply(r,"405 Method Not Allowed");
        if(r->content_len>MaxFile)return reply(r,"413 Payload Too Large","Maximum file size is 512 MiB");
        if(httpd_req_get_hdr_value_len(r,"Content-Range"))return reply(r,"400 Bad Request","Partial PUT is not supported");
        char parent[224];snprintf(parent,sizeof parent,"%s",full);*strrchr(parent,'/')=0;struct stat ps;
        if(!info(parent,ps) || !S_ISDIR(ps.st_mode))return reply(r,"409 Conflict","Parent folder does not exist");
        if(!webdav_recover())return reply(r,"500 Internal Server Error","SD transaction recovery failed");
        int fd=open(Temp,O_WRONLY|O_CREAT|O_EXCL,0666);if(fd<0)return reply(r,"507 Insufficient Storage");
        access.show("Uploading",relative);progress(0,r->content_len);
        constexpr size_t Chunk=256*1024;
        auto buffer=r->content_len?(char *)heap_caps_malloc(Chunk,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):nullptr;
        auto stage=r->content_len?(char *)heap_caps_aligned_alloc(64,1024,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA):nullptr;
        if(r->content_len && (!buffer || !stage)){free(buffer);free(stage);close(fd);unlink(Temp);return reply(r,"503 Service Unavailable","Not enough transfer memory");}
        size_t used=0;bool ok=true;
        // Separate receive and SD-write bursts, as the existing buffered gCAM path does.
        while(ok && used<r->content_len) {
            size_t count=std::min(Chunk,r->content_len-used),received=0;
            while(received<count) {
                int n=httpd_req_recv(r,stage,std::min<size_t>(1024,count-received));
                if(n<=0){ok=false;break;}
                for(int i=0;i<n;i++)((volatile char *)buffer)[received++]=stage[i];
                progress(used+received,r->content_len);
            }
            // SD DMA must use internal aligned memory, as in panel_fs.cpp.
            for(size_t at=0;ok && at<count;at+=1024) {
                size_t n=std::min<size_t>(1024,count-at);
                for(size_t i=0;i<n;i++)((volatile char *)stage)[i]=((const volatile char *)buffer)[at+i];
                ok=write_all(fd,stage,n);
            }
            if(ok)used+=count;
        }
        free(stage);free(buffer);
        if(fsync(fd))ok=false;
        if(close(fd))ok=false;
        if(ok) {ok=commit(Temp,relative,exists);if(ok){storage_remote_invalidate(relative);access.changed=true;}}
        if(!ok){unlink(Temp);return reply(r,used<r->content_len?"400 Bad Request":"507 Insufficient Storage","Upload failed; check the SD card before retrying");}
        access.success=true;return reply(r,exists?"204 No Content":"201 Created");
    }
    if(r->content_len)return reply(r,"415 Unsupported Media Type");
    if(r->method==HTTP_MKCOL) {
        if(exists)return reply(r,"405 Method Not Allowed");
        access.show("Creating folder",relative);
        if(mkdir(full,0775))return reply(r,errno==ENOENT?"409 Conflict":"507 Insufficient Storage");
        access.success=access.changed=true;return reply(r,"201 Created");
    }
    if(!exists)return reply(r,"404 Not Found");
    if(r->method==HTTP_DELETE) {
        access.show("Deleting",relative);storage_remote_invalidate(relative);
        access.changed=true;access.success=remove_tree(full);
        if(access.success)forget_leases(relative);
        return reply(r,access.success?"204 No Content":"500 Internal Server Error");
    }
    char destination[320],host[128],dest[160],target[224],overwrite[8];
    if(!header(r,"Destination",destination,sizeof destination) || !header(r,"Host",host,sizeof host) || !header(r,"Overwrite",overwrite,sizeof overwrite) || !*destination)return reply(r,"400 Bad Request");
    const char *uri=destination;
    if(!strncmp(uri,"http://",7)){uri+=7;const char *slash=strchr(uri,'/');if(!slash || size_t(slash-uri)!=strlen(host) || strncasecmp(uri,host,slash-uri))return reply(r,"502 Bad Gateway");uri=slash;}
    if(!path(uri,dest,sizeof dest) || !strcmp(dest,"/") || !strcasecmp(dest,relative))return reply(r,"403 Forbidden");
    size_t source_len=strlen(relative);
    if((!strncasecmp(dest,relative,source_len) && dest[source_len]=='/') || (!strncasecmp(relative,dest,strlen(dest)) && relative[strlen(dest)]=='/'))return reply(r,"403 Forbidden","Destination is inside source");
    full_path(dest,target,sizeof target);struct stat ds;bool replacing=info(target,ds);
    if(locked(dest,tokens,true) || parent_locked(dest,tokens))return reply(r,"423 Locked");
    if(*overwrite && strcmp(overwrite,"T") && strcmp(overwrite,"F"))return reply(r,"400 Bad Request");
    if(replacing && !strcmp(overwrite,"F"))return reply(r,"412 Precondition Failed");

    if(!webdav_recover())return reply(r,"500 Internal Server Error");
    access.show(r->method==HTTP_MOVE?"Renaming":"Copying",relative);
    if(r->method==HTTP_MOVE)access.success=commit(full,dest,replacing);
    else {
        uint32_t used=0,total=S_ISDIR(s.st_mode)?0:s.st_size;
        auto buffer=(char *)heap_caps_aligned_alloc(64,1024,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);progress(0,total);
        char depth[16];bool ok=buffer && header(r,"Depth",depth,sizeof depth) && (!*depth || !strcmp(depth,"infinity") || !strcmp(depth,"0"));
        if(ok && S_ISDIR(s.st_mode) && !strcmp(depth,"0"))ok=mkdir(Temp,0775)==0;
        else if(ok)ok=copy_tree(full,Temp,buffer,1024,used,total);
        free(buffer);access.success=ok && commit(Temp,dest,replacing);if(!access.success)remove_tree(Temp);
    }
    access.changed=access.success;
    if(access.success){storage_remote_invalidate(relative);storage_remote_invalidate(dest);if(r->method==HTTP_MOVE)forget_leases(relative);}
    return reply(r,access.success?(replacing?"204 No Content":"201 Created"):"409 Conflict");
}
} // namespace
bool webdav_recover() {
    struct stat st;
    if(!info(Work,st) && mkdir(Work,0775))return false;
    if(info(Journal,st)) {
        char relative[160]={},uri[164],target[224];FILE *file=fopen(Journal,"rb");if(!file)return false;
        size_t n=fread(relative,1,sizeof relative-1,file);bool ok=!ferror(file) && n==(size_t)st.st_size && n<sizeof relative;fclose(file);
        snprintf(uri,sizeof uri,"/sd%s",relative);char checked[160];
        if(!ok || !n || !path(uri,checked,sizeof checked) || !strcmp(checked,"/")) {
            if(info(Backup,st))return false;
            return unlink(Journal)==0 && remove_tree(Temp);
        }
        full_path(checked,target,sizeof target);
        if(info(Backup,st) && !info(target,st)){if(rename(Backup,target))return false;}
        else if(!remove_tree(Backup))return false;
        if(unlink(Journal))return false;
    } else if(info(Backup,st))return false; // Never discard an unrecognised recovery copy.
    return remove_tree(Temp);
}
bool webdav_self_test() {
    char out[160];bool ok=true;
    for(auto uri:{"/sd","/sd/","/sd/folder/file.nc","/sd/part%20one.nc","/sd/a%26b.nc"}) ok=path(uri,out,sizeof out) && ok;
    for(auto uri:{"/sdfail/a","/sd/../a","/sd/a/./b","/sd/.PaNeL-previews/a","/sd/.panel-webdav/journal","/sd/a%2fb","/sd/a%5cb","/sd/a%00b","/sd/%252e%252e","/sd/name.","/sd/name%20","/sd/name?query","/sd/m%C3%B6.nc"}) ok=!path(uri,out,sizeof out) && ok;
    auto props=(Properties *)heap_caps_calloc(1,sizeof(Properties),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!props)ok=false;
    else {
        props->all=true;ok=parse_properties("",*props,false) && ok;
        *props=Properties();ok=parse_properties("<d:propfind xmlns:d=\"DAV:\"><d:allprop/></d:propfind>",*props,false) && props->all && ok;
        *props=Properties();ok=parse_properties("<propfind xmlns=\"DAV:\"><prop><getetag/><getcontentlength/></prop></propfind>",*props,false) && props->count==2 && known(props->items[0]) && ok;
        *props=Properties();ok=parse_properties("<d:propfind xmlns:d=\"DAV:\" xmlns:z=\"urn:schemas-microsoft-com:\"><d:prop><z:Win32FileAttributes/></d:prop></d:propfind>",*props,false) && props->count==1 && known(props->items[0]) && ok;
        for(auto invalid:{"<!DOCTYPE x><propfind><allprop/></propfind>","<propfind><allprop/>","<propfind><prop><x:getetag/></prop></propfind>","<propfind><prop><getetag/></wrong></propfind>"}) {
            *props=Properties();ok=!parse_properties(invalid,*props,false) && ok;
        }
    }
    free(props);printf("@UGTEST %s webdav_paths_and_properties\n",ok?"PASS":"FAIL");return ok;
}
void webdav_init() {
    nvs_handle_t h;uint8_t value=0;
    if(nvs_open("panelstorage",NVS_READONLY,&h)==ESP_OK){nvs_get_u8(h,"webdav",&value);nvs_close(h);}enabled=value!=0;
}
void webdav_register(httpd_handle_t server) {
    tag_epoch=esp_random();
    leases=(Lease *)heap_caps_calloc(LeaseCount,sizeof(Lease),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    for(auto uri:{"/sd","/sd/*","/"}) {
        httpd_uri_t route={};route.uri=uri;route.method=(httpd_method_t)HTTP_ANY;route.handler=handle;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server,&route));
    }
}
bool webdav_enabled() {return enabled.load();}
bool webdav_enabled(bool value) {
    if(storage_remote_active())return false;
    nvs_handle_t h;if(nvs_open("panelstorage",NVS_READWRITE,&h)!=ESP_OK)return false;
    bool ok=nvs_set_u8(h,"webdav",value)==ESP_OK && nvs_commit(h)==ESP_OK;nvs_close(h);
    if(ok)enabled=value;
    return ok;
}
void webdav_activity(WebdavActivity &out) {portENTER_CRITICAL(&activity_lock);out=activity;portEXIT_CRITICAL(&activity_lock);}
} // namespace panel

extern "C" bool panel_debug_webdav_test(void) {return panel::webdav_self_test();}
