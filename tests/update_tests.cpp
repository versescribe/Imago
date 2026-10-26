#include "updates.h"
#include "update_version.h"
#include <winrt/base.h>
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string Release(const char* tag,const char* assets="[]") {
    return std::string("{\"draft\":false,\"prerelease\":false,\"tag_name\":\"")+tag+"\",\"assets\":"+assets+"}";
}
int main() {
    winrt::init_apartment();
    try {
        using namespace imago;
        Require(ParseVersion(L"0.0.1").has_value(),"Initial stable version");
        Require(*ParseVersion(L"v0.0.10")>*ParseVersion(L"0.0.9"),"Compare numbers, not strings");
        Require(*ParseVersion(L"1.0.0")>*ParseVersion(L"0.99.99"),"Compare major before minor");
        for(auto text:{L"",L"1.2",L"1.2.3.4",L"1.2.3-beta",L"1.2.3+meta",L"01.2.3",L"1.-2.3",L"65536.0.0",L"v1.2.3/evil",L"1.2.3 "})
            Require(!ParseVersion(text),"Reject malformed or nonstable versions");
        Require(ParseRelease(Release("v0.0.1"),L"0.0.1").state==UpdateState::Current,"Equal release");
        Require(ParseRelease(Release("v0.0.1"),L"0.0.2").state==UpdateState::Current,"Never downgrade");
        Require(ParseRelease(Release("v0.0.2"),L"0.0.1").state==UpdateState::Failed,"No package is not an available update");
        const auto assets=R"([{"name":"Imago-0.0.2-windows-x64.zip","state":"uploaded","size":42}])";
        Require(ParseRelease(Release("v0.0.2",assets),L"0.0.1").state==UpdateState::Available,"Completed Windows package");
        Require(ParseRelease(Release("v0.0.3",assets),L"0.0.1").state==UpdateState::Failed,"Package must match tag");
        Require(ParseRelease(Release("v0.0.2-beta",assets),L"0.0.1").state==UpdateState::Failed,"Reject prerelease tag");
        auto draft=Release("v0.0.2",assets);draft.replace(draft.find("false"),5,"true");
        Require(ParseRelease(draft,L"0.0.1").state==UpdateState::Failed,"Never advertise a draft");
        for(const auto& json:{std::string("<html>bad gateway</html>"),std::string("{}"),std::string(300000,'x')})
            Require(ParseRelease(json,L"0.0.1").state==UpdateState::Failed,"Bounded untrusted JSON");
        std::cout<<"PASS: release metadata, numeric versions, package readiness, malformed input\n";
        winrt::uninit_apartment();return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';winrt::uninit_apartment();return 1;}
}
