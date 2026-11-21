#include "frnl/ui_session.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
using namespace frnl;
template <class Fn> void rejects(Fn fn) { bool threw = false;
  try { fn(); } catch (const std::exception&) { threw = true; } assert(threw); }
int main() {
  ComponentDefinition parent;
  parent.name="Panel"; parent.states["idle"]={};
  ComponentDefinition child;
  child.name="Button";
  child.presentation_role="button";
  child.presentation_label="Default";
  child.states["idle"].events["click"]="click";
  child.states["disabled"].events["click"]="click";
  child.states["idle"].visual_parameters["opacity"]=0.5;
  SemanticRuntime runtime;
  runtime.register_component("page", parent, "idle");
  runtime.register_component("button", child, "idle", "page");
  int click_count=0, frontend_notifications=0;
  runtime.register_handler("click", [&](SemanticRuntime&, const std::string&, const std::any& payload) {
    ++click_count; return std::any(std::any_cast<int>(payload) * 2);
  });
  bool visible=true;
  MeasurementPort measured=[&](const std::string& id) -> std::optional<RealizedState> {
    if (id != "button") return std::nullopt; // fail-closed absent renderer entity
    return RealizedState{true, visible, {}, Geometry{1, 2, 50, 20}, {{"tag","button"}}};
  };
  FrontendApi api(runtime, "NativeApp", "/home", measured);
  auto snapshot=api.inspect();
  assert(snapshot.api_version==1 && snapshot.application_id=="NativeApp");
  assert(snapshot.component_count==2 && snapshot.roots.size()==1 && snapshot.roots[0]=="page");
  auto snap=api.inspect_component("button");
  assert((snap.path==std::vector<std::string>{"page", "button"}));
  assert(snap.presentation.role=="button" && snap.presentation.label=="Default");
  assert(snap.controls.at("click") && snap.realized.mounted && snap.realized.visible);
  assert(snap.realized.geometry && snap.realized.geometry->width==50);
  assert(!api.inspect_component("page").realized.mounted);
  assert(std::any_cast<int>(api.invoke("button","click",3))==6 && click_count==1);
  auto observer=api.subscribe([&](const FrontendSnapshot& value) {
    ++frontend_notifications; assert(value.component_count==2);
  });
  assert(frontend_notifications==1);
  runtime.set_property("button", "aria-label", std::string("Save"));
  assert(frontend_notifications==2 && api.inspect_component("button").presentation.label=="Save");
  api.invoke("button","setVisualParameter",VisualChange{"opacity",1.0});
  assert(std::get<double>(api.inspect_component("button").visual_parameters.at("opacity"))==1.0);
  runtime.register_theme("night",Theme{});
  api.invoke("button","applyTheme",std::string("night"));
  assert(api.inspect_component("button").controls.at("applyTheme"));
  rejects([&] { api.invoke("button","unknown"); });

  auto attrs = Properties{{"role", std::string("button")},{"aria-label",std::string("Save")},
                          {"tabIndex", std::int64_t(0)}};
  AccessibilityContract a11y;
  a11y.role="button"; a11y.name_required=true; a11y.focusable=true;
  a11y.required_attributes={"role"};
  assert(validate_accessibility(a11y,attrs).empty());
  attrs.erase("aria-label");
  assert(validate_accessibility(a11y,attrs).size()==1);
  attrs["role"]=std::string("link"); attrs["tabIndex"]=std::int64_t(-1);
  assert(validate_accessibility(a11y,attrs).size()==3);
  rejects([&] { validate_realized({true,true,{},Geometry{0,0,-1,2},{}}); });

  int calls=0, acked=0, errors=0;
  UiSession session(api, [&](const FrontendSnapshot&, const std::string& revision,
                             const std::optional<UiCompletion>& completion)->std::optional<UiCommand> {
    if (completion) { assert(completion->id=="cmd1" && completion->succeeded); ++acked; }
    if (++calls==1) return UiCommand{"cmd1",revision,"button","click",4};
    return std::nullopt;
  }, [](const ComponentSnapshot&,const std::string& action){return action=="click";},
  [&](std::exception_ptr){++errors;});
  assert(session.tick() && click_count==2 && session.pending_completion()->succeeded);
  assert(!session.tick() && acked==1 && errors==0);

  // No authorization callback is a closed policy, even with a valid revision.
  UiSession default_deny(api, [&](const FrontendSnapshot&,const std::string& revision,
                                  const std::optional<UiCompletion>&) {
    return std::optional<UiCommand>{UiCommand{"deny",revision,"button","click",8}};
  });
  assert(default_deny.tick() && !default_deny.pending_completion()->succeeded && click_count==2);
  // A transport mutation between snapshot and command verification must revoke stale actions.
  UiSession stale(api, [&](const FrontendSnapshot&,const std::string& revision,
                           const std::optional<UiCompletion>&) {
    visible=false;
    return std::optional<UiCommand>{UiCommand{"stale",revision,"button","click",8}};
  },[](const ComponentSnapshot&, const std::string&){return true;});
  assert(stale.tick() && !stale.pending_completion()->succeeded && click_count==2);
  visible=true;
  UiSession policy_race(api, [&](const FrontendSnapshot&,const std::string& revision,
                                 const std::optional<UiCompletion>&) {
    return std::optional<UiCommand>{UiCommand{"policy-race",revision,"button","click",6}};
  },[&](const ComponentSnapshot&,const std::string&) { visible=false; return true; });
  assert(policy_race.tick() && !policy_race.pending_completion()->succeeded && click_count==2);
  visible=true;
  UiSession disabled(api, [&](const FrontendSnapshot&,const std::string& revision,
                              const std::optional<UiCompletion>&) {
    return std::optional<UiCommand>{UiCommand{"disabled",revision,"button","setState",std::string("disabled")}};
  },[](const ComponentSnapshot&,const std::string&){return true;});
  assert(disabled.tick() && !disabled.pending_completion()->succeeded);
  assert(api.inspect_component("button").state=="idle");

  session.stop(); session.stop();
  assert(session.stopped() && !session.tick());
  observer.reset();
  api.dispose(); api.dispose();
  assert(api.disposed());
  rejects([&]{ (void)api.inspect(); });
  rejects([&]{ (void)api.invoke("button","click",1); });
  FrontendSubscription dangling;
  {
    FrontendApi local(runtime);
    dangling=local.subscribe([](const FrontendSnapshot&){});
  }
  dangling.reset();
}
