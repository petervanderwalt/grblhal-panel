// Exclusive WebDAV write leases. The HTTP server serializes access to this table.
struct Lease { char path[160]={},token[64]={};uint32_t expires=0;bool deep=false; };
static Lease *leases;
static constexpr unsigned LeaseCount=8;
static bool within(const char *child,const char *parent) {
    size_t n=strlen(parent);return !strcasecmp(child,parent) || (!strncasecmp(child,parent,n) && (n==1 || child[n]=='/'));
}
static void expire_leases() {
    if(leases)for(unsigned i=0;i<LeaseCount;i++)if(*leases[i].path && (int32_t)(ticks()-leases[i].expires)>=0)leases[i].path[0]=0;
}
static bool covers(const Lease &l,const char *target) {return !strcasecmp(l.path,target) || (l.deep && within(target,l.path));}
static bool locked(const char *target,unsigned tokens,bool descendants=false) {
    expire_leases();
    if(leases)for(unsigned i=0;i<LeaseCount;i++)if(*leases[i].path && !(tokens&(1u<<i)) &&
       (covers(leases[i],target) || (descendants && within(leases[i].path,target))))return true;
    return false;
}
static bool parent_locked(const char *target,unsigned tokens) {
    char parent[160];snprintf(parent,sizeof parent,"%s",target);char *slash=strrchr(parent,'/');
    if(!slash)return false;
    if(slash==parent)parent[1]=0;else *slash=0;
    return locked(parent,tokens);
}
static void forget_leases(const char *target) {
    if(leases)for(unsigned i=0;i<LeaseCount;i++)if(within(leases[i].path,target))leases[i].path[0]=0;
}
static bool local_uri(httpd_req_t *r,const char *value,char *relative,size_t size) {
    if(!strcmp(value,"/")){snprintf(relative,size,"/");return true;}
    if(!strncasecmp(value,"http://",7)) {
        char host[128];if(!header(r,"Host",host,sizeof host))return false;
        value+=7;const char *slash=strchr(value,'/');
        if(!slash || (size_t)(slash-value)!=strlen(host) || strncasecmp(value,host,slash-value))return false;
        value=slash;
    }
    return path(value,relative,size);
}
static bool if_conditions(httpd_req_t *r,const char *source,unsigned &submitted) {
    char text[1024];submitted=0;expire_leases();
    if(!header(r,"If",text,sizeof text))return false;
    if(!*text)return true;
    const char *p=text;char tagged[160];snprintf(tagged,sizeof tagged,"%s",source);
    bool have_group=false,group_pass=false;unsigned group_tokens=0;
    auto space=[&](){while(isspace((unsigned char)*p))p++;};
    auto take=[&](char close,char *out,size_t size){const char *end=strchr(++p,close);if(!end || size_t(end-p)>=size)return false;snprintf(out,size,"%.*s",int(end-p),p);p=end+1;return true;};
    while(*p) {
        space();if(!*p)break;
        if(*p=='<') {
            if(have_group){if(!group_pass)return false;submitted|=group_tokens;}
            char uri[320];if(!take('>',uri,sizeof uri) || !local_uri(r,uri,tagged,sizeof tagged))return false;
            have_group=false;group_pass=false;group_tokens=0;space();
        }
        if(*p++!='(')return false;
        bool pass=true,any=false;unsigned tokens=0;
        while(true) {
            space();if(*p==')'){p++;break;}
            bool negate=!strncmp(p,"Not",3) && isspace((unsigned char)p[3]);if(negate){p+=3;space();}
            char value[160];bool truth=false;unsigned token=0;
            if(*p=='<') {
                if(!take('>',value,sizeof value))return false;
                if(leases)for(unsigned i=0;i<LeaseCount;i++)if(*leases[i].path && !strcmp(value,leases[i].token) && covers(leases[i],tagged)){truth=true;token=1u<<i;}
            } else if(*p=='[') {
                if(!take(']',value,sizeof value))return false;
                char full[224],current[64];struct stat st;full_path(tagged,full,sizeof full);
                if(info(full,st)){etag(st,current,sizeof current);truth=!strcmp(current,value);}
            } else return false;
            any=true;pass&=negate?!truth:truth;if(truth && !negate)tokens|=token;
        }
        if(!any)return false;
        have_group=true;if(pass){group_pass=true;group_tokens|=tokens;}
    }
    if(!have_group || !group_pass)return false;
    submitted|=group_tokens;return true;
}
static unsigned lease_seconds(httpd_req_t *r) {
    char timeout[80];if(!header(r,"Timeout",timeout,sizeof timeout))return 0;
    if(!*timeout || !strcasecmp(timeout,"Infinite"))return 3600;
    if(strncasecmp(timeout,"Second-",7))return 0;
    char *end;unsigned long value=strtoul(timeout+7,&end,10);
    if(end==timeout+7 || (*end && *end!=','))return 0;
    return std::min<unsigned long>(3600,std::max(1ul,value));
}
