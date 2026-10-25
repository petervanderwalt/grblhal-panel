#include "panel_file_head.h"
#include <assert.h>
#include <string.h>
#include <string>
static std::string preview(const std::string &code) {
    FILE *file = tmpfile(); assert(file);
    assert(fwrite(code.data(), 1, code.size(), file) == code.size()); rewind(file);
    char text[1536]; assert(panel::file_head(file, text, sizeof text)); fclose(file);
    return text;
}
int main() {
    assert(preview("(header)\r\nG21\r\n\r\nG90\n") == "(header)\nG21\n\nG90");
    assert(preview(std::string(200, 'X') + "\nM30") == std::string(77, 'X') + "...\nM30");
    std::string code; for (int i=0;i<20;++i) code += "G1X1\n";
    auto text=preview(code); unsigned lines=1; for(char c:text) if(c=='\n')++lines; assert(lines==14);
    assert(preview(std::string(5000,'X')+"\nM30") == std::string(77,'X')+"...");
    assert(preview("G1\tX1\x01") == "G1 X1?");
    char small[5]={'?','?','?','?','!'};FILE *file=tmpfile();fputs("G21G90",file);rewind(file);
    assert(panel::file_head(file,small,4));assert(small[3]==0 && small[4]=='!');fclose(file);
    assert(!panel::file_head(nullptr,small,sizeof small));
    puts("PASS bounded file-start reader: headers, CRLF, blanks, long lines, byte/line limits and small buffers");
}
