#pragma once
#include "util/Text.h"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <variant>
#include <vector>

namespace lvk::config {
// Small, strict JSON value/parser for configuration data. Never parse JSON with regex.
class Json {
public:
    using Object=std::map<std::string,Json>;
    using Array=std::vector<Json>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> value=nullptr;
    Json()=default;
    Json(bool v):value(v){}
    Json(int v):value(static_cast<double>(v)){}
    Json(double v):value(v){}
    Json(std::string v):value(std::move(v)){}
    Json(const char* v):value(std::string(v)){}
    Json(Array v):value(std::move(v)){}
    Json(Object v):value(std::move(v)){}
    bool has(const std::string& k) const {const auto* o=std::get_if<Object>(&value);return o&&o->contains(k);}
    const Json& at(const std::string& k) const {static const Json empty;const auto* o=std::get_if<Object>(&value);if(!o)return empty;auto i=o->find(k);return i==o->end()?empty:i->second;}
    std::string str(std::string fallback={}) const {const auto* v=std::get_if<std::string>(&value);return v?*v:fallback;}
    double num(double fallback=0) const {const auto* v=std::get_if<double>(&value);return v?*v:fallback;}
    int integer(int fallback) const {auto d=num(fallback);if(d<-2147483648.0||d>2147483647.0||std::floor(d)!=d)throw std::runtime_error("Invalid integer in config.");return static_cast<int>(d);}
    bool flag(bool fallback=false) const {const auto* v=std::get_if<bool>(&value);return v?*v:fallback;}
    const Array& array() const {static const Array empty;const auto* v=std::get_if<Array>(&value);return v?*v:empty;}
    std::string dump() const {
        if(std::holds_alternative<std::nullptr_t>(value))return "null";
        if(auto v=std::get_if<bool>(&value))return *v?"true":"false";
        if(auto v=std::get_if<double>(&value)){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<*v;return s.str();}
        if(auto v=std::get_if<std::string>(&value)){
            std::string out="\"";const char* hex="0123456789abcdef";
            for(unsigned char c:*v){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=c;}
            return out+'"';
        }
        std::string out;bool first=true;
        if(auto v=std::get_if<Array>(&value)){out="[";for(auto& x:*v){if(!first)out+=",";first=false;out+=x.dump();}return out+"]";}
        out="{\n";for(auto& [k,v]:std::get<Object>(value)){if(!first)out+=",\n";first=false;out+="  "+Json(k).dump()+": "+v.dump();}return out+"\n}";
    }
    static Json parse(const std::string& text) {
        struct Parser {
            const std::string& s;size_t p=0;
            [[noreturn]] void fail(){throw std::runtime_error("Invalid config JSON near byte "+std::to_string(p));}
            void ws(){while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'))++p;}
            bool take(char c){ws();if(p<s.size()&&s[p]==c){++p;return true;}return false;}
            unsigned hex(){unsigned v=0;for(int n=0;n<4;++n){if(p==s.size())fail();char c=s[p++];v*=16;if(c>='0'&&c<='9')v+=c-'0';else if(c>='a'&&c<='f')v+=c-'a'+10;else if(c>='A'&&c<='F')v+=c-'A'+10;else fail();}return v;}
            std::string string(){
                if(!take('"'))fail();std::string out;
                while(p<s.size()){
                    unsigned char c=s[p++];if(c=='"'){util::wide(out);return out;}if(c<32)fail();
                    if(c!='\\'){out+=c;continue;}if(p==s.size())fail();
                    switch(s[p++]){
                    case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;
                    case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                    case 'u':{unsigned u=hex();std::wstring w(1,static_cast<wchar_t>(u));
                        if(u>=0xd800&&u<=0xdbff){if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')fail();unsigned lo=hex();if(lo<0xdc00||lo>0xdfff)fail();w+=static_cast<wchar_t>(lo);}
                        else if(u>=0xdc00&&u<=0xdfff)fail();
                        out+=util::utf8(w);break;}
                    default:fail();
                    }
                }fail();
            }
            Json read(int depth=0){
                if(depth>64)fail();ws();if(p==s.size())fail();
                if(s[p]=='"')return Json(string());
                if(take('{')){Object out;if(take('}'))return out;do{auto key=string();if(!take(':')||out.contains(key))fail();out.emplace(key,read(depth+1));}while(take(','));if(!take('}'))fail();return out;}
                if(take('[')){Array out;if(take(']'))return out;do{out.push_back(read(depth+1));}while(take(','));if(!take(']'))fail();return out;}
                for(auto word:{"true","false","null"}){size_t n=std::char_traits<char>::length(word);if(s.compare(p,n,word)==0){p+=n;if(word[0]=='n')return {};return Json(word[0]=='t');}}
                size_t begin=p;if(s[p]=='-')++p;if(p==s.size())fail();
                if(s[p]=='0')++p;else {if(s[p]<'1'||s[p]>'9')fail();while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}
                if(p<s.size()&&s[p]=='.'){++p;size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(start==p)fail();}
                if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(start==p)fail();}
                double d{};auto r=std::from_chars(s.data()+begin,s.data()+p,d);if(r.ec!=std::errc{}||r.ptr!=s.data()+p||!std::isfinite(d))fail();return Json(d);
            }
        } parser{text};
        if(text.starts_with("\xef\xbb\xbf"))parser.p=3;
        auto out=parser.read();parser.ws();if(parser.p!=text.size())parser.fail();return out;
    }
};
}
