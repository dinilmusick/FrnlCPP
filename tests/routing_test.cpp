#include "frnl/routing.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
using namespace frnl;
template<class Fn> void rejects(Fn fn) { bool threw=false; try{fn();}
  catch(const std::exception&){threw=true;} assert(threw); }
int main() {
  Route user{"user","/users/:id", [] {return std::any(42);}};
  Router router; router.add_route(user);
  auto found=router.match("https://example.invalid/users/a%2Fb?mode=fast&mode=slow&msg=two+words&path=/tmp/a#hash");
  assert(found && found->id=="user" && found->params.at("id")=="a/b");
  assert(found->query.at("mode")=="slow" && found->query.at("msg")=="two words");
  assert(found->query.at("path")=="/tmp/a");
  assert(found->path=="/users/a%2Fb");
  assert(!router.match("/different/42"));
  assert(std::any_cast<int>(router.resolve("/users/hello").second)==42);
  assert(build_route_url(user,{{"id","a/b"}},{{"q","two words"}})=="/users/a%2Fb?q=two+words");
  assert(build_route_url(user,{{"id","X"},{"extra","value"}},{},{false,true})=="/users/X?extra=value");
  rejects([&]{build_route_url(user,{{"extra","x"}});});
  rejects([&]{build_route_url(user,{{"id","x"},{"extra","x"}},{},{true,false});});
  rejects([&]{router.add_route({"user","/another",{}});});
  rejects([&]{router.add_route({"other","users/:id",{}});});
  rejects([&]{router.match("/users/%GG");});
  router.add_route({"files","/files/*tail",{}});
  assert(build_route_url(router.route("files"),{{"tail","a/b c"}})=="/files/a/b%20c");
  auto file=router.match("/files/a/b%20c");
  assert(file && file->params.at("tail")=="a/b c");
  router.add_route({"root","/",{}});
  auto root=router.match("/");
  assert(root && root->id=="root");
  assert(build_route_url(router.route("root"))=="/");
  auto absolute_root = match_route(router.route("root"), "https://example.invalid?query=x+y");
  assert(absolute_root && absolute_root->query.at("query") == "x y");
  router.clear();
  assert(!router.match("/users/x"));
}
