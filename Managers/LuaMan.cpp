#include "LuaMan.h"
#include "System.h"

#include "ACDropShip.h"
#include "ACrab.h"
#include "ACraft.h"
#include "ACRocket.h"
#include "Actor.h"
#include "ActorEditor.h"
#include "ADoor.h"
#include "AEmitter.h"
#include "Emission.h"
#include "AHuman.h"
#include "Arm.h"
#include "AtomGroup.h"
#include "Attachable.h"
#include "HDFirearm.h"
#include "HeldDevice.h"
#include "Leg.h"
#include "LimbPath.h"
#include "Magazine.h"
#include "Material.h"
#include "MOSParticle.h"
#include "MOPixel.h"
#include "MOSprite.h"
#include "MOSRotating.h"
#include "Scene.h"
#include "SLTerrain.h"
#include "TerrainObject.h"
#include "SoundContainer.h"
#include "TDExplosive.h"
#include "ThrownDevice.h"
#include "Turret.h"

#include "DataModule.h"
#include "GAScripted.h"
#include "Box.h"
#include "BuyMenuGUI.h"
#include "SceneEditorGUI.h"
#include "MetaPlayer.h"
#include "GUIBanner.h"

#include "MetaMan.h"
#include "ConsoleMan.h"
#include "PresetMan.h"
#include "PrimitiveMan.h"
#include "UInputMan.h"
#include "SettingsMan.h"

extern "C"
{
  #include "lua.h"
  #include "lualib.h"
  #include "lauxlib.h"
}

// LuaBind
#include "luabind/luabind.hpp"
#include "luabind/operator.hpp"
#include "luabind/adopt_policy.hpp"
#include "luabind/out_value_policy.hpp"
#include "luabind/iterator_policy.hpp"
#include "luabind/return_reference_to_policy.hpp"
// Boost
//#include "boost/detail/shared_ptr_nmt.hpp"
//#include "boost/shared_ptr.hpp"

using namespace luabind;

/// <summary>
/// Special callback function for adding file name and line number to error messages when calling functions incorrectly.
/// </summary>
/// <param name="pState">The Lua master state.</param>
/// <returns>An error signal, 1, so Lua correctly reports that there's been an error.</returns>
int AddFileAndLineToError(lua_State* pState) {
    lua_Debug luaDebug;
    if (lua_getstack(pState, 2, &luaDebug) > 0) {
        lua_getinfo(pState, "Sln", &luaDebug);
        std::string errorString = lua_tostring(pState, -1);

        if (errorString.find(".lua") != std::string::npos) {
            lua_pushstring(pState, errorString.c_str());
        } else {
            std::stringstream messageStream;
            messageStream << ((luaDebug.name == nullptr || strstr(luaDebug.name, ".rte") == nullptr) ? luaDebug.short_src : luaDebug.name);
            messageStream << ":" << luaDebug.currentline << ": " << errorString;
            lua_pushstring(pState, messageStream.str().c_str());
        }
    }

   return 1;
}


// From LuaBind documentation:
// You also have to supply two functions for your smart pointer.
// One that returns the type of const version of the smart pointer type (boost::shared_ptr<const A> in this case).
// And one function that extracts the raw pointer from the smart pointer.
// The first function is needed because luabind has to allow the non-const -> conversion when passing values from Lua to C++.
// The second function is needed when Lua calls member functions on held types, the this pointer must be a raw pointer, it is also needed to allow the smart_pointer -> raw_pointer conversion from Lua to C++.

namespace luabind
{
    template<class T>
    T * get_pointer(boost::shared_ptr<T> &p)
    {
        return p.get();
    }

// From LuaBind documentation:
// IMPORTANT
// get_const_holder() has been removed.
// Automatic conversions between smart_ptr<X> and smart_ptr<X const> no longer work.
    /*template<class A>
    boost::shared_ptr<const A> * get_const_holder(boost::shared_ptr<A> *)
    {
        return 0;
    }*/
}


namespace RTE
{

const string LuaMan::m_ClassName = "LuaMan";

// Can't have global enums in the master state so we use this dummy struct as a class and register the enums under it.
struct enum_wrapper {
	// Nested structs for each enum because we can't register enum_wrapper multiple times under a different name.
	// We're doing this so we can access each enum separately by name rather than having all of them accessed from a shared name.
	// If this proves to be a hassle then we can easily revert to the shared name access by registering everything under enum_wrapper.
	struct input_device {};
	struct input_elements {};
	struct mouse_buttons {};
	struct joy_buttons {};
	struct joy_directions {};
};

//////////////////////////////////////////////////////////////////////////////////////////
// Preset clone adapters that will return the exact pre-cast types so we don't have to do:
// myNewActor = ToActor(PresetMan:GetPreset("AHuman", "Soldier Light", "All")):Clone()
// but can instead do:
// myNewActor = CreateActor("Soldier Light", "All");
// or even:
// myNewActor = CreateActor("Soldier Light");
// or for a randomly selected Preset within a group:
// myNewActor = RandomActor("Light Troops");

#define LUAENTITYCREATE(TYPE) \
    TYPE * Create##TYPE(std::string preset, std::string module) \
    { \
        const Entity *pPreset = g_PresetMan.GetEntityPreset(#TYPE, preset, module); \
        if (!pPreset) \
        { \
            g_ConsoleMan.PrintString(string("ERROR: There is no ") + string(#TYPE) + string(" of the Preset name \"") + preset + string("\" defined in the \"") + module + string("\" Data Module!")); \
            return 0; \
        } \
        return dynamic_cast<TYPE *>(pPreset->Clone()); \
    } \
    TYPE * Create##TYPE(std::string preset) { return Create##TYPE(preset, "All"); } \
    TYPE * Random##TYPE(std::string group, int moduleSpaceID) \
    { \
        const Entity *pPreset = g_PresetMan.GetRandomBuyableOfGroupFromTech(group, #TYPE, moduleSpaceID); \
        if (!pPreset) \
            pPreset = g_PresetMan.GetRandomBuyableOfGroupFromTech(group, #TYPE, g_PresetMan.GetModuleID("Base.rte")); \
        if (!pPreset) \
            pPreset = g_PresetMan.GetRandomBuyableOfGroupFromTech("Any", #TYPE, moduleSpaceID); \
        if (!pPreset) \
        { \
            g_ConsoleMan.PrintString(string("ERROR: Could not find any ") + string(#TYPE) + string(" defined in a Group called \"") + group + string("\" in module ") + g_PresetMan.GetDataModuleName(moduleSpaceID) + string("!")); \
            return 0; \
        } \
        return dynamic_cast<TYPE *>(pPreset->Clone()); \
    } \
    TYPE * Random##TYPE(std::string group, std::string module) \
    { \
        int moduleSpaceID = g_PresetMan.GetModuleID(module); \
        const Entity *pPreset = g_PresetMan.GetRandomBuyableOfGroupFromTech(group, #TYPE, moduleSpaceID); \
        if (!pPreset) \
            pPreset = g_PresetMan.GetRandomBuyableOfGroupFromTech(group, #TYPE, g_PresetMan.GetModuleID("Base.rte")); \
        if (!pPreset) \
            pPreset = g_PresetMan.GetRandomBuyableOfGroupFromTech("Any", #TYPE, moduleSpaceID); \
        if (!pPreset) \
        { \
            g_ConsoleMan.PrintString(string("ERROR: Could not find any ") + string(#TYPE) + string(" defined in a Group called \"") + group + string("\" in module ") + module + string("!")); \
            return 0; \
        } \
        return dynamic_cast<TYPE *>(pPreset->Clone()); \
    } \
    TYPE * Random##TYPE(std::string group) { return Random##TYPE(group, "All"); }

// These are expanded by the preprocessor to all the different cloning function definitions.
LUAENTITYCREATE(Attachable)
LUAENTITYCREATE(AEmitter)
LUAENTITYCREATE(Turret)
LUAENTITYCREATE(Actor)
LUAENTITYCREATE(ADoor)
LUAENTITYCREATE(AHuman)
LUAENTITYCREATE(ACrab)
LUAENTITYCREATE(ACraft)
LUAENTITYCREATE(ACDropShip)
LUAENTITYCREATE(ACRocket)
LUAENTITYCREATE(MOSParticle)
LUAENTITYCREATE(MOSRotating)
LUAENTITYCREATE(MOPixel)
LUAENTITYCREATE(Scene)
LUAENTITYCREATE(HeldDevice)
LUAENTITYCREATE(Round)
LUAENTITYCREATE(Magazine)
LUAENTITYCREATE(HDFirearm)
LUAENTITYCREATE(ThrownDevice)
LUAENTITYCREATE(TDExplosive)
LUAENTITYCREATE(TerrainObject)


//////////////////////////////////////////////////////////////////////////////////////////
// Clone adapters that will return the exact pre-cast types so we don't have to do:
// myOtherActor = ToActor(myActor:Clone());
// but can instead do:
// myOtherActor = myActor:Clone();

// Preprocessor helper function so we don't need to maintain a dozen almost identical definitions
#define LUAENTITYCLONE(TYPE) \
    TYPE * Clone##TYPE(const TYPE *pThis) \
        { \
            if (pThis) \
                return dynamic_cast<TYPE *>(pThis->Clone()); \
            else \
                g_ConsoleMan.PrintString(string("ERROR: Tried to clone a ") + string(#TYPE) + string(" reference that is nil!")); \
            return 0; \
        }

// These are expanded by the preprocessor to all the different cloning function definitions.
LUAENTITYCLONE(Entity)
LUAENTITYCLONE(SceneObject)
LUAENTITYCLONE(MovableObject)
LUAENTITYCLONE(Attachable)
LUAENTITYCLONE(AEmitter)
LUAENTITYCLONE(Turret)
LUAENTITYCLONE(Actor)
LUAENTITYCLONE(ADoor)
LUAENTITYCLONE(AHuman)
LUAENTITYCLONE(ACrab)
LUAENTITYCLONE(ACraft)
LUAENTITYCLONE(ACDropShip)
LUAENTITYCLONE(ACRocket)
LUAENTITYCLONE(MOSParticle)
LUAENTITYCLONE(MOSRotating)
LUAENTITYCLONE(MOPixel)
LUAENTITYCLONE(Scene)
LUAENTITYCLONE(HeldDevice)
LUAENTITYCLONE(Round)
LUAENTITYCLONE(Magazine)
LUAENTITYCLONE(HDFirearm)
LUAENTITYCLONE(ThrownDevice)
LUAENTITYCLONE(TDExplosive)
LUAENTITYCLONE(TerrainObject)

//////////////////////////////////////////////////////////////////////////////////////////
// Explicit deletion of any Entity instance that Lua owns.. it will probably be handled
// by the GC, but this makes it instantaneous

void DeleteEntity(Entity *pEntity)
{
    delete pEntity;
    pEntity = 0;
}


//////////////////////////////////////////////////////////////////////////////////////////
// "Downcast" conversion functions from Entity to common derived types

// Preprocessor helper function so we don't need to maintain a dozen almost identical definitions
#define LUAENTITYCAST(TYPE) \
    TYPE * To##TYPE(Entity *pEntity) \
    { \
        TYPE *pTarget = dynamic_cast<TYPE *>(pEntity); \
        if (!pTarget) \
            g_ConsoleMan.PrintString(string("ERROR: Tried to convert a non-") + string(#TYPE) + string(" Entity reference to an ") + string(#TYPE) + string(" reference!")); \
        return pTarget; \
    } \
    const TYPE * ToConst##TYPE(const Entity *pEntity) \
    { \
        const TYPE *pTarget = dynamic_cast<const TYPE *>(pEntity); \
        if (!pTarget) \
            g_ConsoleMan.PrintString(string("ERROR: Tried to convert a non-") + string(#TYPE) + string(" Entity reference to an ") + string(#TYPE) + string(" reference!")); \
        return pTarget; \
    } \
    bool Is##TYPE(Entity *pEntity) { return dynamic_cast<TYPE *>(pEntity) ? true : false; }

// These are expanded by the preprocessor to all the different casting function definitions named: To[Type]()
LUAENTITYCAST(SceneObject)
LUAENTITYCAST(MovableObject)
LUAENTITYCAST(Entity)
LUAENTITYCAST(Attachable)
LUAENTITYCAST(Arm)
LUAENTITYCAST(Leg)
LUAENTITYCAST(Emission)
LUAENTITYCAST(AEmitter)
LUAENTITYCAST(Turret)
LUAENTITYCAST(Actor)
LUAENTITYCAST(ADoor)
LUAENTITYCAST(AHuman)
LUAENTITYCAST(ACrab)
LUAENTITYCAST(ACraft)
LUAENTITYCAST(ACDropShip)
LUAENTITYCAST(ACRocket)
LUAENTITYCAST(MOSParticle)
LUAENTITYCAST(MOSRotating)
LUAENTITYCAST(MOPixel)
LUAENTITYCAST(MOSprite)
LUAENTITYCAST(Scene)
LUAENTITYCAST(Deployment)
LUAENTITYCAST(GameActivity)
LUAENTITYCAST(GlobalScript)
LUAENTITYCAST(GAScripted)
LUAENTITYCAST(HeldDevice)
LUAENTITYCAST(Round)
LUAENTITYCAST(Magazine)
LUAENTITYCAST(HDFirearm)
LUAENTITYCAST(ThrownDevice)
LUAENTITYCAST(TDExplosive)
LUAENTITYCAST(TerrainObject)


//////////////////////////////////////////////////////////////////////////////////////////
// Common LuaBind bindings for abstract and concrete Entities

#define ABSTRACTLUABINDING(TYPE, PARENT) \
    def((string("To") + string(#TYPE)).c_str(), (TYPE *(*)(Entity *))&To##TYPE), \
    def((string("To") + string(#TYPE)).c_str(), (const TYPE *(*)(const Entity *))&ToConst##TYPE), \
    class_<TYPE, PARENT/*, boost::shared_ptr<Entity> */>(#TYPE) \
        .property("ClassName", &TYPE::GetClassName)

#define CONCRETELUABINDING(TYPE, PARENT) \
    def((string("Create") + string(#TYPE)).c_str(), (TYPE *(*)(string, string))&Create##TYPE, adopt(result)), \
    def((string("Create") + string(#TYPE)).c_str(), (TYPE *(*)(string))&Create##TYPE, adopt(result)), \
    def((string("Random") + string(#TYPE)).c_str(), (TYPE *(*)(string, int))&Random##TYPE, adopt(result)), \
    def((string("Random") + string(#TYPE)).c_str(), (TYPE *(*)(string, string))&Random##TYPE, adopt(result)), \
    def((string("Random") + string(#TYPE)).c_str(), (TYPE *(*)(string))&Random##TYPE, adopt(result)), \
    def((string("To") + string(#TYPE)).c_str(), (TYPE *(*)(Entity *))&To##TYPE), \
    def((string("To") + string(#TYPE)).c_str(), (const TYPE *(*)(const Entity *))&ToConst##TYPE), \
    def((string("Is") + string(#TYPE)).c_str(), (bool(*)(const Entity *))&Is##TYPE), \
    class_<TYPE, PARENT/*, boost::shared_ptr<Entity> */>(#TYPE) \
        .def("Clone", &Clone##TYPE, adopt(result)) \
        .property("ClassName", &TYPE::GetClassName)


//////////////////////////////////////////////////////////////////////////////////////////
// Other misc adapters to eliminate/emulate default parameters etc

void GibThis(MOSRotating *pThis) { pThis->GibThis(); }
void AddMO(MovableMan &This, MovableObject *pMO)
{
    if (This.ValidMO(pMO))
        g_ConsoleMan.PrintString("ERROR: Tried to add a MovableObject that already exists in the simulation! " + pMO->GetPresetName());
    else
        This.AddMO(pMO);
}
void AddActor(MovableMan &This, Actor *pActor)
{
    if (This.IsActor(pActor))
        g_ConsoleMan.PrintString("ERROR: Tried to add an Actor that already exists in the simulation!" + pActor->GetPresetName());
    else
        This.AddActor(pActor);
}
void AddItem(MovableMan &This, MovableObject *pItem)
{
    if (This.ValidMO(pItem))
        g_ConsoleMan.PrintString("ERROR: Tried to add an Item that already exists in the simulation!" + pItem->GetPresetName());
    else
        This.AddItem(pItem);
}
void AddParticle(MovableMan &This, MovableObject *pParticle)
{
    if (This.ValidMO(pParticle))
        g_ConsoleMan.PrintString("ERROR: Tried to add a Particle that already exists in the simulation!" + pParticle->GetPresetName());
    else
        This.AddParticle(pParticle);
}

/*
//////////////////////////////////////////////////////////////////////////////////////////
// Wrapper for the GAScripted so we can derive new classes from it purely in lua:
//
// "It is also possible to derive Lua classes from C++ classes, and override virtual functions with Lua functions.
// To do this we have to create a wrapper class for our C++ base class.
// This is the class that will hold the Lua object when we instantiate a Lua class"

struct GAScriptedWrapper:
    GAScripted,
    wrap_base
{
    GAScriptedWrapper(): GAScripted() { ; }

    // Passing in the path of the script file that defines, in Lua, the GAScripted-derived class
//    virtual int Create(const GAScripted &reference) { return call<int>("Create", reference); }
    virtual Entity * Clone(Entity *pCloneTo = 0) const { return call<Entity *>("Clone", pCloneTo); }
    virtual int Start() { call<int>("Start"); }
    virtual void Pause(bool pause) { call<void>("Pause", pause); }
    virtual void End() { call<void>("End"); }
    virtual void Update() { call<void>("Update"); }

//    static int static_Create(GAScripted *pSelf, const GAScripted &reference) { return pSelf->GAScripted::Create(reference); }
    static Entity * static_Clone(GAScripted *pSelf, Entity *pCloneTo = 0) { return pSelf->GAScripted::Clone(pCloneTo); }
    static int static_Start(GAScripted *pSelf) { return pSelf->GAScripted::Start(); }
    static void static_Pause(GAScripted *pSelf, bool pause) { return pSelf->GAScripted::Pause(pause); }
    static void static_End(GAScripted *pSelf) { return pSelf->GAScripted::End(); }
    static void static_Update(GAScripted *pSelf) { return pSelf->GAScripted::Update(); }
};
*/

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::Clear()
{
    m_pMasterState = 0;
    m_LastError.clear();
// TODO: is this the best way to give ID's.. won't ever be reset?
    m_NextPresetID = 0;
    m_NextObjectID = 0;
    m_pTempEntity = 0;
    m_TempEntityVector.clear();
    m_TempEntityVector.shrink_to_fit();

	//Clear files list
	for (int i = 0; i < MAX_OPEN_FILES; ++i)
		m_Files[i] = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int LuaMan::Create()
{
    // Create the master state
    m_pMasterState = lua_open();
    // Attach the master state to LuaBind
    open(m_pMasterState);
    // Open the lua libs for the master state
    //luaL_openlibs(m_pMasterState);

	// Load only libraries we need
	lua_pushcfunction(m_pMasterState, luaopen_base);
	lua_pushliteral(m_pMasterState, LUA_COLIBNAME);
	lua_call(m_pMasterState, 1, 0);

	lua_pushcfunction(m_pMasterState, luaopen_table);
	lua_pushliteral(m_pMasterState, LUA_TABLIBNAME);
	lua_call(m_pMasterState, 1, 0);

	lua_pushcfunction(m_pMasterState, luaopen_string);
	lua_pushliteral(m_pMasterState, LUA_STRLIBNAME);
	lua_call(m_pMasterState, 1, 0);

	lua_pushcfunction(m_pMasterState, luaopen_math);
	lua_pushliteral(m_pMasterState, LUA_MATHLIBNAME);
	lua_call(m_pMasterState, 1, 0);

	lua_pushcfunction(m_pMasterState, luaopen_debug);
	lua_pushliteral(m_pMasterState, LUA_DBLIBNAME);
	lua_call(m_pMasterState, 1, 0);

	lua_pushcfunction(m_pMasterState, luaopen_package);
	lua_pushliteral(m_pMasterState, LUA_LOADLIBNAME);
	lua_call(m_pMasterState, 1, 0);


    // From LuaBind documentation:
    // As mentioned in the Lua documentation, it is possible to pass an error handler function to lua_pcall().
    // Luabind makes use of lua_pcall() internally when calling member functions and free functions.
    // It is possible to set the error handler function that Luabind will use globally:
    //set_pcall_callback(&AddFileAndLineToError); //NOTE: This seems to do nothing

    // Declare all useful classes in the master state
    module(m_pMasterState)
    [
        class_<Vector>("Vector")
            .def(luabind::constructor<>())
            .def(luabind::constructor<float, float>())
            .def(self == other<const Vector &>())
            .def(const_self + other<const Vector &>())
            .def(const_self - other<const Vector &>())
            .def(const_self * float())
            .def(const_self / float())
            .def(tostring(const_self))
            .property("ClassName", &Vector::GetClassName)
            .property("RoundedX", &Vector::GetRoundIntX)
            .property("RoundedY", &Vector::GetRoundIntY)
            .property("Rounded", &Vector::GetRounded)
            .property("FlooredX", &Vector::GetFloorIntX)
            .property("FlooredY", &Vector::GetFloorIntY)
            .property("Floored", &Vector::GetFloored)
            .property("CeilingedX", &Vector::GetCeilingIntX)
            .property("CeilingedY", &Vector::GetCeilingIntY)
            .property("Ceilinged", &Vector::GetCeilinged)
            .property("Magnitude", &Vector::GetMagnitude)
            .def("SetMagnitude", &Vector::SetMagnitude)
            .property("Largest", &Vector::GetLargest)
            .property("Smallest", &Vector::GetSmallest)
            .property("Normalized", &Vector::GetNormalized)
            .property("Perpendicular", &Vector::GetPerpendicular)
            .def("GetXFlipped", &Vector::GetXFlipped)
            .def("GetYFlipped", &Vector::GetYFlipped)
            .property("AbsRadAngle", &Vector::GetAbsRadAngle)
            .property("AbsDegAngle", &Vector::GetAbsDegAngle)
            .def("CapMagnitude", &Vector::CapMagnitude)
            .def("FlipX", &Vector::FlipX)
            .def("FlipY", &Vector::FlipY)
            .def("IsZero", &Vector::IsZero)
            .def("IsOpposedTo", &Vector::IsOpposedTo)
            .def("Dot", &Vector::Dot)
            .def("Cross", &Vector::Cross)
            .def("Round", &Vector::Round)
            .def("ToHalf", &Vector::ToHalf)
            .def("Floor", &Vector::Floor)
            .def("Ceiling", &Vector::Ceiling)
            .def("Normalize", &Vector::Normalize)
            .def("Perpendicularize", &Vector::Perpendicularize)
            .def("Reset", &Vector::Reset)
            .def("RadRotate", &Vector::RadRotate)
            .def("DegRotate", &Vector::DegRotate)
            .def("AbsRotateTo", &Vector::AbsRotateTo)
            .def_readwrite("X", &Vector::m_X)
            .def_readwrite("Y", &Vector::m_Y)
            .def("SetXY", &Vector::SetXY),

        class_<Box>("Box")
            .def(constructor<>())
            .def(constructor<const Vector &, const Vector &>())
            .def(constructor<float, float, float, float>())
            .def(constructor<const Vector &, float, float>())
            .def(constructor<const Box &>())
            .def(self == other<const Box &>())
            .property("ClassName", &Box::GetClassName)
            .property("Corner", &Box::GetCorner, &Box::SetCorner)
            .property("Width", &Box::GetWidth, &Box::SetWidth)
            .property("Height", &Box::GetHeight, &Box::SetHeight)
            .property("Center", &Box::GetCenter, &Box::SetCenter)
            .property("Area", &Box::GetArea)
            .def("GetRandomPoint", &Box::GetRandomPoint)
            .def("Unflip", &Box::Unflip)
            .def("IsWithinBox", &Box::IsWithinBox)
            .def("IsWithinBoxX", &Box::IsWithinBoxX)
            .def("IsWithinBoxY", &Box::IsWithinBoxY)
            .def("GetWithinBoxX", &Box::GetWithinBoxX)
            .def("GetWithinBoxY", &Box::GetWithinBoxY)
            .def("GetWithinBox", &Box::GetWithinBox)
            .def("IntersectsBox", &Box::IntersectsBox),

        class_<Scene::Area>("Area")
            .def(constructor<>())
            .def(constructor<string>())
            .def(constructor<const Scene::Area &>())
            .def("Reset", &Scene::Area::Reset)
            .property("ClassName", &Scene::Area::GetClassName)
            .property("Name", &Scene::Area::GetName)
            .def("AddBox", &Scene::Area::AddBox)
            .def("HasNoArea", &Scene::Area::HasNoArea)
            .def("IsInside", &Scene::Area::IsInside)
            .def("IsInsideX", &Scene::Area::IsInsideX)
            .def("IsInsideY", &Scene::Area::IsInsideY)
            .def("GetBoxInside", &Scene::Area::GetBoxInside)
            .def("RemoveBoxInside", &Scene::Area::RemoveBoxInside)
            .def("GetCenterPoint", &Scene::Area::GetCenterPoint)
            .def("GetRandomPoint", &Scene::Area::GetRandomPoint),

        class_<Entity/*, boost::shared_ptr<Entity> */>("Entity")
            .def("Clone", &CloneEntity)
            .def("Reset", &Entity::Reset)
            .def(tostring(const_self))
            .property("ClassName", &Entity::GetClassName)
            .property("PresetName", &Entity::GetPresetName, &Entity::SetPresetName)
            .property("Description", &Entity::GetDescription, &Entity::SetDescription)
            .def("GetModuleAndPresetName", &Entity::GetModuleAndPresetName)
            .property("IsOriginalPreset", &Entity::IsOriginalPreset)
            .property("ModuleID", &Entity::GetModuleID)
			.property("RandomWeight", &Entity::GetRandomWeight)
            .def("AddToGroup", &Entity::AddToGroup)
            .def("IsInGroup", (bool (Entity::*)(const string &))&Entity::IsInGroup),

		class_<SoundContainer>("SoundContainer")
			.def(constructor<>())
            .def("HasAnySounds", &SoundContainer::HasAnySounds)
			.def("IsBeingPlayed", &SoundContainer::IsBeingPlayed)
            .def("Play", (bool (SoundContainer:: *)()) &SoundContainer::Play)
            .def("Play", (bool (SoundContainer:: *)(const Vector &position)) &SoundContainer::Play)
            .def("Play", (bool (SoundContainer:: *)(const Vector &position, int player)) &SoundContainer::Play)
            .def("Stop", (bool (SoundContainer:: *)()) &SoundContainer::Stop)
            .def("Stop", (bool (SoundContainer:: *)(int player)) &SoundContainer::Stop)
            .def("AddSound", (void (SoundContainer:: *)(std::string const &soundFilePath)) &SoundContainer::AddSound)
            .def("AddSound", (void (SoundContainer:: *)(std::string const &soundFilePath, const Vector &offset, float attenuationStartDistance, bool abortGameForInvalidSound)) &SoundContainer::AddSound)
            .def("AddSound", (void (SoundContainer:: *)(std::string const &soundFilePath, unsigned int soundSetIndex, const Vector &offset, float minimumAudibleDistance, float attenuationStartDistance, bool abortGameForInvalidSound)) &SoundContainer::AddSound)
			.def("SetPosition", &SoundContainer::SetPosition)
            .def("SelectNextSoundSet", &SoundContainer::SelectNextSoundSet)
			.property("Loops", &SoundContainer::GetLoopSetting, &SoundContainer::SetLoopSetting)
            .property("Priority", &SoundContainer::GetPriority, &SoundContainer::SetPriority)
            .property("AffectedByGlobalPitch", &SoundContainer::IsAffectedByGlobalPitch, &SoundContainer::SetAffectedByGlobalPitch),

        ABSTRACTLUABINDING(SceneObject, Entity)
            .property("Pos", &SceneObject::GetPos, &SceneObject::SetPos)
            .property("HFlipped", &SceneObject::IsHFlipped, &SceneObject::SetHFlipped)
            .property("RotAngle", &SceneObject::GetRotAngle, &SceneObject::SetRotAngle)
            .property("Team", &SceneObject::GetTeam, &SceneObject::SetTeam)
            .property("PlacedByPlayer", &SceneObject::GetPlacedByPlayer, &SceneObject::SetPlacedByPlayer)
            .def("GetGoldValue", &SceneObject::GetGoldValueOld)
            .def("GetGoldValue", &SceneObject::GetGoldValue)
			.def("SetGoldValue", &SceneObject::SetGoldValue)
			.def("GetGoldValueString", &SceneObject::GetGoldValueString)
            .def("GetTotalValue", &SceneObject::GetTotalValueOld)
            .def("GetTotalValue", &SceneObject::GetTotalValue)
            .property("IsBuyable", &SceneObject::IsBuyable)
            .def("IsOnScenePoint", &SceneObject::IsOnScenePoint),

        ABSTRACTLUABINDING(MovableObject, SceneObject)
			.def("GetParent", (MovableObject * (MovableObject::*)())&MovableObject::GetParent)
			.def("GetParent", (const MovableObject * (MovableObject::*)() const)&MovableObject::GetParent)
			.def("GetRootParent", (MovableObject * (MovableObject::*)())&MovableObject::GetRootParent)
			.def("GetRootParent", (const MovableObject * (MovableObject::*)() const)&MovableObject::GetRootParent)
			.property("Material", &MovableObject::GetMaterial)
            .def("ReloadScripts", (int (MovableObject:: *)()) &MovableObject::ReloadScripts)
            .def("HasScript", &MovableObject::HasScript)
            .def("AddScript", &MovableObject::AddScript)
            .def("ScriptEnabled", &MovableObject::ScriptEnabled)
            .def("EnableScript", &MovableObject::EnableScript)
            .def("DisableScript", &MovableObject::DisableScript)
            .property("Mass", &MovableObject::GetMass, &MovableObject::SetMass)
            .property("Pos", &MovableObject::GetPos, &MovableObject::SetPos)
            .property("Vel", &MovableObject::GetVel, &MovableObject::SetVel)
            .property("AngularVel", &MovableObject::GetAngularVel, &MovableObject::SetAngularVel)
            .property("Radius", &MovableObject::GetRadius)
            .property("Diameter", &MovableObject::GetDiameter)
            .property("Scale", &MovableObject::GetScale, &MovableObject::SetScale)
            .property("EffectRotAngle", &MovableObject::GetEffectRotAngle, &MovableObject::SetEffectRotAngle)
            .property("GlobalAccScalar", &MovableObject::GetGlobalAccScalar, &MovableObject::SetGlobalAccScalar)
            .property("AirResistance", &MovableObject::GetAirResistance, &MovableObject::SetAirResistance)
            .property("AirThreshold", &MovableObject::GetAirThreshold, &MovableObject::SetAirThreshold)
            .property("Age", &MovableObject::GetAge, &MovableObject::SetAge)
            .property("Lifetime", &MovableObject::GetLifetime, &MovableObject::SetLifetime)
            .property("ID", &MovableObject::GetID)
            .property("UniqueID", &MovableObject::GetUniqueID)
            .property("RootID", &MovableObject::GetRootID)
            .property("MOIDFootprint", &MovableObject::GetMOIDFootprint)
            .property("Sharpness", &MovableObject::GetSharpness, &MovableObject::SetSharpness)
            .def("GetAltitude", &MovableObject::GetAltitude)
            .property("AboveHUDPos", &MovableObject::GetAboveHUDPos)
            .property("HitsMOs", &MovableObject::HitsMOs, &MovableObject::SetToHitMOs)
            .property("GetsHitByMOs", &MovableObject::GetsHitByMOs, &MovableObject::SetToGetHitByMOs)
            .property("IgnoresTeamHits", &MovableObject::IgnoresTeamHits, &MovableObject::SetIgnoresTeamHits)
            .property("IgnoresWhichTeam", &MovableObject::IgnoresWhichTeam)
			.property("IgnoreTerrain", &MovableObject::IgnoreTerrain, &MovableObject::SetIgnoreTerrain)
			.def("SetWhichMOToNotHit", &MovableObject::SetWhichMOToNotHit)
            .property("ToSettle", &MovableObject::ToSettle, &MovableObject::SetToSettle)
            .property("ToDelete", &MovableObject::ToDelete, &MovableObject::SetToDelete)
            .def("IsSetToDelete", &MovableObject::IsSetToDelete)
			.property("MissionCritical", &MovableObject::IsMissionCritical, &MovableObject::SetMissionCritical)
			.def("IsMissionCritical", &MovableObject::IsMissionCritical)
			.property("HUDVisible", &MovableObject::GetHUDVisible, &MovableObject::SetHUDVisible)
            .def("IsGeneric", &MovableObject::IsGeneric)
            .def("IsActor", &MovableObject::IsActor)
            .def("IsDevice", &MovableObject::IsDevice)
            .def("IsHeldDevice", &MovableObject::IsHeldDevice)
            .def("IsThrownDevice", &MovableObject::IsThrownDevice)
            .def("IsGold", &MovableObject::IsGold)
            .def("IsThrownDevice", &MovableObject::IsThrownDevice)
            .def("HasObject", &MovableObject::HasObject)
            .def("HasObjectInGroup", &MovableObject::HasObjectInGroup)
            .def("AddForce", &MovableObject::AddForce)
            .def("AddAbsForce", &MovableObject::AddAbsForce)
            .def("AddImpulseForce", &MovableObject::AddImpulseForce)
            .def("AddAbsImpulseForce", &MovableObject::AddAbsImpulseForce)
            .def("ClearForces", &MovableObject::ClearForces)
            .def("ClearImpulseForces", &MovableObject::ClearImpulseForces)
			.def("GetForcesCount", &MovableObject::GetForcesCount)
			.def("GetForceVector", &MovableObject::GetForceVector)
			.def("GetForceOffset", &MovableObject::GetForceOffset)
			.def("SetForceVector", &MovableObject::SetForceVector)
			.def("SetForceOffset", &MovableObject::SetForceOffset)
			.def("GetImpulsesCount", &MovableObject::GetImpulsesCount)
			.def("GetImpulseVector", &MovableObject::GetImpulseVector)
			.def("GetImpulseOffset", &MovableObject::GetImpulseOffset)
			.def("SetImpulseVector", &MovableObject::SetImpulseVector)
			.def("SetImpulseOffset", &MovableObject::SetImpulseOffset)
            .property("PinStrength", &MovableObject::GetPinStrength, &MovableObject::SetPinStrength)
            .def("RestDetection", &MovableObject::RestDetection)
            .def("NotResting", &MovableObject::NotResting)
            .def("IsAtRest", &MovableObject::IsAtRest)
            .def("MoveOutOfTerrain", &MovableObject::MoveOutOfTerrain)
            .def("RotateOffset", &MovableObject::RotateOffset)
			.property("DamageOnCollision", &MovableObject::DamageOnCollision, &MovableObject::SetDamageOnCollision)
			.property("DamageOnPenetration", &MovableObject::DamageOnPenetration, &MovableObject::SetDamageOnPenetration)
			.property("WoundDamageMultiplier", &MovableObject::WoundDamageMultiplier, &MovableObject::SetWoundDamageMultiplier)
			.property("HitWhatMOID", &MovableObject::HitWhatMOID)
			.property("HitWhatTerrMaterial", &MovableObject::HitWhatTerrMaterial)
			.property("ProvidesPieMenuContext", &MovableObject::ProvidesPieMenuContext, &MovableObject::SetProvidesPieMenuContext)
			.property("HitWhatParticleUniqueID", &MovableObject::HitWhatParticleUniqueID),

		class_<Material, Entity>("Material")
			.property("ID", &Material::GetIndex)
			.property("Restitution", &Material::GetRestitution)
			.property("Bounce", &Material::GetRestitution)
			.property("Friction", &Material::GetFriction)
			.property("Stickiness", &Material::GetStickiness)
			.property("Strength", &Material::GetIntegrity)
			.property("StructuralIntegrity", &Material::GetIntegrity)
			.property("DensityKGPerVolumeL", &Material::GetVolumeDensity)
			.property("DensityKGPerPixel", &Material::GetPixelDensity)
			.property("SettleMaterial", &Material::GetSettleMaterial)
			.property("SpawnMaterial", &Material::GetSpawnMaterial)
			.property("TransformsInto", &Material::GetSpawnMaterial)
			.property("IsScrap", &Material::IsScrap),

        CONCRETELUABINDING(MOPixel, MovableObject),

        CONCRETELUABINDING(TerrainObject, SceneObject)
            .def("GetBitmapOffset", &TerrainObject::GetBitmapOffset)
            .def("GetBitmapWidth", &TerrainObject::GetBitmapWidth)
            .def("GetBitmapHeight", &TerrainObject::GetBitmapHeight),

        ABSTRACTLUABINDING(MOSprite, MovableObject)
            .enum_("SpriteAnimMode")
            [
                value("NOANIM", 0),
                value("ALWAYSLOOP", 1),
                value("ALWAYSRANDOM", 2),
                value("ALWAYSPINGPONG", 3),
                value("LOOPWHENMOVING", 4),
                value("LOOPWHENOPENCLOSE", 5),
                value("PINGPONGOPENCLOSE", 6),
                value("OVERLIFETIME", 7),
                value("ONCOLLIDE", 8)
            ]
            .property("Diameter", &MOSprite::GetDiameter)
            .property("BoundingBox", &MOSprite::GetBoundingBox)
            .property("FrameCount", &MOSprite::GetFrameCount)
            .property("SpriteOffset", &MOSprite::GetSpriteOffset, &MOSprite::SetSpriteOffset)
            .property("HFlipped", &MOSprite::IsHFlipped, &MOSprite::SetHFlipped)
            .property("FlipFactor", &MOSprite::GetFlipFactor)
            .property("RotAngle", &MOSprite::GetRotAngle, &MOSprite::SetRotAngle)
            .property("AngularVel", &MOSprite::GetAngularVel, &MOSprite::SetAngularVel)
            .property("Frame", &MOSprite::GetFrame, &MOSprite::SetFrame)
            .property("SpriteAnimMode", &MOSprite::GetSpriteAnimMode, &MOSprite::SetSpriteAnimMode)
            .property("SpriteAnimDuration", &MOSprite::GetSpriteAnimDuration, &MOSprite::SetSpriteAnimDuration)
            .def("SetNextFrame", &MOSprite::SetNextFrame)
            .def("IsTooFast", &MOSprite::IsTooFast)
            .def("IsOnScenePoint", &MOSprite::IsOnScenePoint)
            .def("RotateOffset", &MOSprite::RotateOffset)
            .def("UnRotateOffset", &MOSprite::UnRotateOffset)
            .def("GetSpriteWidth", &MOSprite::GetSpriteWidth)
            .def("GetSpriteHeight", &MOSprite::GetSpriteHeight)
			.def("SetEntryWound", &MOSprite::SetEntryWound)
			.def("SetExitWound", &MOSprite::SetExitWound)
			.def("GetEntryWoundPresetName", &MOSprite::GetEntryWoundPresetName)
			.def("GetExitWoundPresetName", &MOSprite::GetExitWoundPresetName),

        CONCRETELUABINDING(MOSParticle, MOSprite),

        CONCRETELUABINDING(MOSRotating, MOSprite)
            /*.property("Material", &MOSRotating::GetMaterial)*/
            .property("RecoilForce", &MOSRotating::GetRecoilForce)
            .property("RecoilOffset", &MOSRotating::GetRecoilOffset)
			.property("TravelImpulse", &MOSRotating::GetTravelImpulse, &MOSRotating::SetTravelImpulse)
			.property("GibWoundLimit", &MOSRotating::GetGibWoundLimit, &MOSRotating::SetGibWoundLimit)
			.property("GibImpulseLimit", &MOSRotating::GetGibImpulseLimit, &MOSRotating::SetGibImpulseLimit)
			.property("DamageMultiplier", &MOSRotating::GetDamageMultiplier, &MOSRotating::SetDamageMultiplier)
			.property("WoundCount", &MOSRotating::GetWoundCount)
            .def("AddRecoil", &MOSRotating::AddRecoil)
            .def("SetRecoil", &MOSRotating::SetRecoil)
            .def("IsRecoiled", &MOSRotating::IsRecoiled)
            .def("EnableDeepCheck", &MOSRotating::EnableDeepCheck)
            .def("ForceDeepCheck", &MOSRotating::ForceDeepCheck)
            .def("GibThis", &MOSRotating::GibThis)
            // Free function bound as member function to emulate default variables
            .def("GibThis", &GibThis)
            .def("MoveOutOfTerrain", &MOSRotating::MoveOutOfTerrain)
            .def("ApplyForces", &MOSRotating::ApplyForces)
            .def("ApplyImpulses", &MOSRotating::ApplyImpulses)
			.def("AddWound", &MOSRotating::AddWound, adopt(_2))
			.def("RemoveWounds", &MOSRotating::RemoveWounds)
            .def("IsOnScenePoint", &MOSRotating::IsOnScenePoint)
            .def("EraseFromTerrain", &MOSRotating::EraseFromTerrain)
            .def("GetStringValue", &MOSRotating::GetStringValue)
            .def("GetNumberValue", &MOSRotating::GetNumberValue)
            .def("GetObjectValue", &MOSRotating::GetObjectValue)
            .def("SetStringValue", &MOSRotating::SetStringValue)
            .def("SetNumberValue", &MOSRotating::SetNumberValue)
            .def("SetObjectValue", &MOSRotating::SetObjectValue)
            .def("RemoveStringValue", &MOSRotating::RemoveStringValue)
            .def("RemoveNumberValue", &MOSRotating::RemoveNumberValue)
            .def("RemoveObjectValue", &MOSRotating::RemoveObjectValue)
            .def("StringValueExists", &MOSRotating::StringValueExists)
            .def("NumberValueExists", &MOSRotating::NumberValueExists)
            .def("ObjectValueExists", &MOSRotating::ObjectValueExists)
            .def("AddAttachable", (void (MOSRotating::*)(Attachable *attachableToAdd))&MOSRotating::AddAttachable, adopt(_2))
            .def("AddAttachable", (void (MOSRotating::*)(Attachable *attachableToAdd, const Vector &parentOffset))&MOSRotating::AddAttachable, adopt(_2))
            .def("RemoveAttachable", (bool (MOSRotating::*)(Attachable *attachableToRemove))&MOSRotating::RemoveAttachable)
            .def("RemoveAttachable", (bool (MOSRotating::*)(long uniqueIDOfAttachableToRemove))&MOSRotating::RemoveAttachable)
			.def("AddEmitter", (void (MOSRotating::*)(Attachable *attachableToAdd))&MOSRotating::AddAttachable, adopt(_2))
			.def("AddEmitter", (void (MOSRotating::*)(Attachable *attachableToAdd, const Vector &parentOffset))&MOSRotating::AddAttachable, adopt(_2))
			.def("RemoveEmitter", (bool (MOSRotating::*)(Attachable *attachableToRemove))&MOSRotating::RemoveAttachable)
			.def("RemoveEmitter", (bool (MOSRotating::*)(long uniqueIDOfAttachableToRemove))&MOSRotating::RemoveAttachable)
			.def_readonly("Attachables", &MOSRotating::m_AllAttachables, return_stl_iterator)
			.def_readonly("Wounds", &MOSRotating::m_Wounds, return_stl_iterator),

        CONCRETELUABINDING(Attachable, MOSRotating)
			.property("ParentOffset", &Attachable::GetParentOffset, &Attachable::SetParentOffset)
            .property("JointOffset", &Attachable::GetJointOffset, &Attachable::SetJointOffset)
            .property("JointStiffness", &Attachable::GetJointStiffness, &Attachable::SetJointStiffness)
            .property("JointStrength", &Attachable::GetJointStrength, &Attachable::SetJointStrength)
            .property("RotTarget", &Attachable::GetRotTarget, &Attachable::SetRotTarget)
            .property("AtomSubgroupID", &Attachable::GetAtomSubgroupID, &Attachable::SetAtomSubgroupID)
            .property("OnlyLinearForces", &Attachable::GetOnlyLinearForces, &Attachable::SetOnlyLinearForces)
            .def("IsAttached", &Attachable::IsAttached)
            .def("IsAttachedTo", &Attachable::IsAttachedTo)
            .def("IsDrawnAfterParent", &Attachable::IsDrawnAfterParent)
            .def("TransferJointForces", &Attachable::TransferJointForces)
            .def("TransferJointImpulses", &Attachable::TransferJointImpulses)
            .def("CollectDamage", &Attachable::CollectDamage)
			.property("InheritsRotAngle", &Attachable::InheritsRotAngle, &Attachable::SetInheritsRotAngle)
			.property("IsCollidingWithTerrainWhileAttached", &Attachable::IsCollidingWithTerrainWhileAttached)
			.def("EnableTerrainCollisions", &Attachable::EnableTerrainCollisions),

		ABSTRACTLUABINDING(Emission, Entity)
			.property("ParticlesPerMinute", &Emission::GetRate, &Emission::SetRate)
			.property("MinVelocity", &Emission::GetMinVelocity, &Emission::SetMinVelocity)
			.property("MaxVelocity", &Emission::GetMaxVelocity, &Emission::SetMaxVelocity)
			.property("PushesEmitter", &Emission::PushesEmitter, &Emission::SetPushesEmitter)
			.property("LifeVariation", &Emission::GetLifeVariation, &Emission::SetLifeVariation)
			.property("BurstSize", &Emission::GetBurstSize, &Emission::SetBurstSize)
			.property("Spread", &Emission::GetSpread, &Emission::SetSpread)
			.property("Offset", &Emission::GetOffset, &Emission::SetOffset)
			.def("ResetEmissionTimers", &Emission::ResetEmissionTimers),

        CONCRETELUABINDING(AEmitter, Attachable)
            .def("IsEmitting", &AEmitter::IsEmitting)
            .def("EnableEmission", &AEmitter::EnableEmission)
            .property("BurstScale", &AEmitter::GetBurstScale, &AEmitter::SetBurstScale)
            .property("EmitAngle", &AEmitter::GetEmitAngle, &AEmitter::SetEmitAngle)
            .property("GetThrottle", &AEmitter::GetThrottle, &AEmitter::SetThrottle)
            .property("Throttle", &AEmitter::GetThrottle, &AEmitter::SetThrottle)
            .property("BurstSpacing", &AEmitter::GetBurstSpacing, &AEmitter::SetBurstSpacing)
			.property("BurstDamage", &AEmitter::GetBurstDamage, &AEmitter::SetBurstDamage)
			.property("EmitterDamageMultiplier", &AEmitter::GetEmitterDamageMultiplier, &AEmitter::SetEmitterDamageMultiplier)
			.property("EmitCountLimit", &AEmitter::GetEmitCountLimit, &AEmitter::SetEmitCountLimit)
			.property("EmitDamage", &AEmitter::GetEmitDamage, &AEmitter::SetEmitDamage)
			.property("FlashScale", &AEmitter::GetFlashScale, &AEmitter::SetFlashScale)
			.def("GetEmitVector", &AEmitter::GetEmitVector)
            .def("GetRecoilVector", &AEmitter::GetRecoilVector)
            .def("EstimateImpulse", &AEmitter::EstimateImpulse)
            .def("TriggerBurst", &AEmitter::TriggerBurst)
            .def("IsSetToBurst", &AEmitter::IsSetToBurst)
            .def("CanTriggerBurst", &AEmitter::CanTriggerBurst)
			.def_readwrite("Emissions", &AEmitter::m_EmissionList, return_stl_iterator),


        CONCRETELUABINDING(Actor, MOSRotating)
            .enum_("Status")
            [
                value("STABLE", 0),
                value("UNSTABLE", 1),
                value("INACTIVE", 2),
                value("DYING", 3),
                value("DEAD", 4)
            ]
            .enum_("AIMode")
            [
                value("AIMODE_NONE", 0),
                value("AIMODE_SENTRY", 1),
                value("AIMODE_PATROL", 2),
                value("AIMODE_GOTO", 3),
                value("AIMODE_BRAINHUNT", 4),
                value("AIMODE_GOLDDIG", 5),
                value("AIMODE_RETURN", 6),
                value("AIMODE_STAY", 7),
                value("AIMODE_SCUTTLE", 8),
                value("AIMODE_DELIVER", 9),
                value("AIMODE_BOMB", 10),
                value("AIMODE_SQUAD", 11),
                value("AIMODE_COUNT", 12)
            ]
            .enum_("ActionState")
            [
                value("MOVING", 0),
                value("MOVING_FAST", 1),
                value("FIRING", 2),
                value("ActionStateCount", 3)
            ]
            .enum_("AimState")
            [
                value("AIMSTILL", 0),
                value("AIMUP", 1),
                value("AIMDOWN", 2),
                value("AimStateCount", 3)
            ]
            .enum_("LateralMoveState")
            [
                value("LAT_STILL", 0),
                value("LAT_LEFT", 1),
                value("LAT_RIGHT", 2)
            ]
            .enum_("ObstacleState")
            [
                value("PROCEEDING", 0),
                value("BACKSTEPPING", 1),
                value("DIGPAUSING", 2),
                value("JUMPING", 3),
                value("SOFTLANDING", 4)
            ]
            .enum_("TeamBlockState")
            [
                value("NOTBLOCKED", 0),
                value("BLOCKED", 1),
                value("IGNORINGBLOCK", 2),
                value("FOLLOWWAIT", 3)
            ]
            .def(constructor<>())
            .def("GetController", &Actor::GetController)
            .def("IsPlayerControlled", &Actor::IsPlayerControlled)
            .def("IsControllable", &Actor::IsControllable)
            .def("SetControllerMode", &Actor::SetControllerMode)
            .def("SwapControllerModes", &Actor::SwapControllerModes)
			.property("ImpulseDamageThreshold", &Actor::GetTravelImpulseDamage, &Actor::SetTravelImpulseDamage)
			.def("GetStableVelocityThreshold", &Actor::GetStableVel)
			.def("SetStableVelocityThreshold", (void (Actor::*)(float, float))&Actor::SetStableVel)
			.def("SetStableVelocityThreshold", (void (Actor::*)(Vector))&Actor::SetStableVel)
            .property("Status", &Actor::GetStatus, &Actor::SetStatus)
            .property("Health", &Actor::GetHealth, &Actor::SetHealth)
            .property("MaxHealth", &Actor::GetMaxHealth, &Actor::SetMaxHealth)
            .property("GoldCarried", &Actor::GetGoldCarried, &Actor::SetGoldCarried)
            .property("AimRange", &Actor::GetAimRange, &Actor::SetAimRange)
            .def("GetAimAngle", &Actor::GetAimAngle)
            .def("SetAimAngle", &Actor::SetAimAngle)
            .def("HasObject", &Actor::HasObject)
            .def("HasObjectInGroup", &Actor::HasObjectInGroup)
            .property("CPUPos", &Actor::GetCPUPos)
            .property("EyePos", &Actor::GetEyePos)
            .property("ViewPoint", &Actor::GetViewPoint, &Actor::SetViewPoint)
            .property("Height", &Actor::GetHeight)
            .def("IsWithinRange", &Actor::IsWithinRange)
            .def("AddHealth", &Actor::AddHealth)
            .def("IsStatus", &Actor::IsStatus)
            .def("IsDead", &Actor::IsDead)
            .def("FacingAngle", &Actor::FacingAngle)
            .property("AIMode", &Actor::GetAIMode, &Actor::SetAIMode)
            .property("DeploymentID", &Actor::GetDeploymentID)
            .property("PassengerSlots", &Actor::GetPassengerSlots, &Actor::SetPassengerSlots)
			.def("AddAISceneWaypoint", &Actor::AddAISceneWaypoint)
            .def("AddAIMOWaypoint", &Actor::AddAIMOWaypoint)
            .def("ClearAIWaypoints", &Actor::ClearAIWaypoints)
            .def("GetLastAIWaypoint", &Actor::GetLastAIWaypoint)
            .def("GetAIMOWaypointID", &Actor::GetAIMOWaypointID)
            .def("GetWaypointListSize", &Actor::GetWaypointsSize)
            .def("ClearMovePath", &Actor::ClearMovePath)
            .def("AddToMovePathBeginning", &Actor::AddToMovePathBeginning)
            .def("AddToMovePathEnd", &Actor::AddToMovePathEnd)
            .def("RemoveMovePathBeginning", &Actor::RemoveMovePathBeginning)
            .def("RemoveMovePathEnd", &Actor::RemoveMovePathEnd)
            .property("Perceptiveness", &Actor::GetPerceptiveness, &Actor::SetPerceptiveness)
            .def("AddInventoryItem", &Actor::AddInventoryItem, adopt(_2))
            .def("RemoveInventoryItem", &Actor::RemoveInventoryItem)
            .def("SwapNextInventory", &Actor::SwapNextInventory)
            .def("SwapPrevInventory", &Actor::SwapPrevInventory)
            .def("DropAllInventory", &Actor::DropAllInventory)
            .property("InventorySize", &Actor::GetInventorySize)
            .def("IsInventoryEmpty", &Actor::IsInventoryEmpty)
            .property("MaxMass", &Actor::GetMaxMass)
            .def("FlashWhite", &Actor::FlashWhite)
            .def("DrawWaypoints", &Actor::DrawWaypoints)
            .def("SetMovePathToUpdate", &Actor::SetMovePathToUpdate)
            .def("UpdateMovePath", &Actor::UpdateMovePath)
            .property("MovePathSize", &Actor::GetMovePathSize)
            .def_readwrite("MOMoveTarget", &Actor::m_pMOMoveTarget)
            .def_readwrite("MovePath", &Actor::m_MovePath, return_stl_iterator)
            .def_readwrite("Inventory", &Actor::m_Inventory, return_stl_iterator)
            .def("SetAlarmPoint", &Actor::AlarmPoint)
            .def("GetAlarmPoint", &Actor::GetAlarmPoint)
            .property("AimDistance", &Actor::GetAimDistance, &Actor::SetAimDistance)
			.property("SightDistance", &Actor::GetSightDistance, &Actor::SetSightDistance)
			.property("TotalWoundCount", &Actor::GetTotalWoundCount)
			.property("TotalWoundLimit", &Actor::GetTotalWoundLimit)
            .def("RemoveAnyRandomWounds", &Actor::RemoveAnyRandomWounds)
            .property("DeathSound", &Actor::GetDeathSound, &Actor::SetDeathSound),

        CONCRETELUABINDING(ADoor, Actor)
			.enum_("DooorState")
			[
				value("CLOSED", 0),
				value("OPENING", 1),
				value("OPEN", 2),
				value("CLOSING", 3),
				value("STOPPED", 4)
			]
            .property("Door", &ADoor::GetDoor)
			.def("GetDoorState", &ADoor::GetDoorState)
			.def("OpenDoor", &ADoor::OpenDoor)
			.def("CloseDoor", &ADoor::CloseDoor)
			.def("StopDoor", &ADoor::StopDoor)
			.def("SetClosedByDefault", &ADoor::SetClosedByDefault),

		ABSTRACTLUABINDING(Arm, Attachable)
			.property("IdleOffset", &Arm::GetIdleOffset, &Arm::SetIdleOffset)
			.property("HandPos", &Arm::GetHandPos, &Arm::SetHandPos),


        ABSTRACTLUABINDING(Leg, Attachable)
            .property("Foot", &Leg::GetFoot),

        CONCRETELUABINDING(AHuman, Actor)
            .enum_("UpperBodyState")
            [
                value("WEAPON_READY", 0),
                value("AIMING_SHARP", 1),
                value("HOLSTERING_BACK", 2),
                value("HOLSTERING_BELT", 3),
                value("DEHOLSTERING_BACK", 4),
                value("DEHOLSTERING_BELT", 5),
                value("THROWING_PREP", 6),
                value("THROWING_RELEASE", 7)
            ]
            .enum_("MovementState")
            [
                value("NOMOVE", 0),
                value("STAND", 1),
                value("WALK", 2),
                value("CROUCH", 3),
                value("CRAWL", 4),
                value("ARMCRAWL", 5),
                value("CLIMB", 6),
                value("JUMP", 7),
                value("DISLODGE", 8),
                value("MOMENTSTATECOUNT", 9)
            ]
            .enum_("ProneState")
            [
                value("NOTPRONE", 0),
                value("GOPRONE", 1),
                value("PRONE", 2),
                value("PRONESTATECOUNT", 3)
            ]
            .enum_("Layer")
            [
                value("FGROUND", 0),
                value("BGROUND", 1)
            ]
            .enum_("DeviceHandlingState")
            [
                value("STILL", 0),
                value("POINTING", 1),
                value("SCANNING", 2),
                value("AIMING", 3),
                value("FIRING", 4),
                value("THROWING", 5),
                value("DIGGING", 6)
            ]
            .enum_("SweepState")
            [
                value("NOSWEEP", 0),
                value("SWEEPINGUP", 1),
                value("SWEEPINGPAUSE", 2),
                value("SWEEPINGDOWN", 3),
                value("SWEEMDOWNPAUSE", 4)
            ]
            .enum_("DigState")
            [
                value("NOTDIGGING", 0),
                value("PREDIG", 1),
                value("STARTDIG", 2),
                value("TUNNELING", 3),
                value("FINISHINGDIG", 4),
                value("PAUSEDIGGER", 5)
            ]
            .enum_("JumpState")
            [
                value("NOTJUMPING", 0),
                value("FORWARDJUMP", 1),
                value("PREJUMP", 2),
                value("UPJUMP", 3),
                value("APEXJUMP", 4),
                value("LANDJUMP", 5)
            ]
            .def(constructor<>())
            .property("Head", &AHuman::GetHead)
            .property("FGArm", &AHuman::GetFGArm)
            .property("BGArm", &AHuman::GetBGArm)
            .property("FGLeg", &AHuman::GetFGLeg)
			.property("FGFoot", &AHuman::GetFGFoot)
            .property("BGLeg", &AHuman::GetBGLeg)
			.property("BGFoot", &AHuman::GetBGFoot)
            .property("Jetpack", &AHuman::GetJetpack)
            .property("JetTimeTotal", &AHuman::GetJetTimeTotal, &AHuman::SetJetTimeTotal)
            .property("JetTimeLeft", &AHuman::GetJetTimeLeft, &AHuman::SetJetTimeLeft)
			.property("ThrowPrepTime", &AHuman::GetThrowPrepTime, &AHuman::SetThrowPrepTime)
            .def("EquipFirearm", &AHuman::EquipFirearm)
            .def("EquipThrowable", &AHuman::EquipThrowable)
            .def("EquipDiggingTool", &AHuman::EquipDiggingTool)
            .def("EquipShield", &AHuman::EquipShield)
            .def("EquipShieldInBGArm", &AHuman::EquipShieldInBGArm)
            .def("EquipDeviceInGroup", &AHuman::EquipDeviceInGroup)
            .def("EquipNamedDevice", &AHuman::EquipNamedDevice)
            .def("EquipLoadedFirearmInGroup", &AHuman::EquipLoadedFirearmInGroup)
            .def("UnequipBGArm", &AHuman::UnequipBGArm)
            .property("EquippedItem", &AHuman::GetEquippedItem)
            .property("EquippedBGItem", &AHuman::GetEquippedBGItem)
            .property("FirearmIsReady", &AHuman::FirearmIsReady)
            .property("ThrowableIsReady", &AHuman::ThrowableIsReady)
            .property("FirearmIsEmpty", &AHuman::FirearmIsEmpty)
            .property("FirearmNeedsReload", &AHuman::FirearmNeedsReload)
            .property("FirearmIsSemiAuto", &AHuman::FirearmIsSemiAuto)
            .property("FirearmActivationDelay", &AHuman::FirearmActivationDelay)
            .def("ReloadFirearm", &AHuman::ReloadFirearm)
            .def("IsWithinRange", &AHuman::IsWithinRange)
            .def("Look", &AHuman::Look)
            .def("LookForGold", &AHuman::LookForGold)
            .def("LookForMOs", &AHuman::LookForMOs)
            .def("IsOnScenePoint", &AHuman::IsOnScenePoint)
			.property("LimbPathPushForce", &AHuman::GetLimbPathPushForce, &AHuman::SetLimbPathPushForce)
			.def("GetLimbPathSpeed", &AHuman::GetLimbPathSpeed)
			.def("SetLimbPathSpeed", &AHuman::SetLimbPathSpeed),
        
		CONCRETELUABINDING(ACrab, Actor)
            .enum_("MovementState")
            [
                value("STAND", 0),
                value("WALK", 1),
                value("JUMP", 2),
                value("DISLODGE", 3),
                value("MOMENTSTATECOUNT", 4)
            ]
            .enum_("Side")
            [
                value("LEFTSIDE", 0),
                value("RIGHTSIDE", 1),
                value("SIDECOUNT", 2)
            ]
            .enum_("Layer")
            [
                value("FGROUND", 0),
                value("BGROUND", 1)
            ]
            .enum_("DeviceHandlingState")
            [
                value("STILL", 0),
                value("POINTING", 1),
                value("SCANNING", 2),
                value("AIMING", 3),
                value("FIRING", 4),
                value("THROWING", 5),
                value("DIGGING", 6)
            ]
            .enum_("SweepState")
            [
                value("NOSWEEP", 0),
                value("SWEEPINGUP", 1),
                value("SWEEPINGPAUSE", 2),
                value("SWEEPINGDOWN", 3),
                value("SWEEMDOWNPAUSE", 4)
            ]
            .enum_("DigState")
            [
                value("NOTDIGGING", 0),
                value("PREDIG", 1),
                value("STARTDIG", 2),
                value("TUNNELING", 3),
                value("FINISHINGDIG", 4),
                value("PAUSEDIGGER", 5)
            ]
            .enum_("JumpState")
            [
                value("NOTJUMPING", 0),
                value("FORWARDJUMP", 1),
                value("PREJUMP", 2),
                value("UPJUMP", 3),
                value("APEXJUMP", 4),
                value("LANDJUMP", 5)
            ]
            .def(constructor<>())
            .property("Turret", &ACrab::GetTurret)
            .property("LFGLeg", &ACrab::GetLFGLeg)
            .property("LBGLeg", &ACrab::GetLBGLeg)
            .property("RFGLeg", &ACrab::GetRFGLeg)
            .property("RBGLeg", &ACrab::GetRBGLeg)
            .property("Jetpack", &ACrab::GetJetpack)
            .property("JetTimeTotal", &ACrab::GetJetTimeTotal, &ACrab::SetJetTimeTotal)
            .property("JetTimeLeft", &ACrab::GetJetTimeLeft)
            .property("EquippedItem", &ACrab::GetEquippedItem)
            .property("FirearmIsReady", &ACrab::FirearmIsReady)
            .property("FirearmIsEmpty", &ACrab::FirearmIsEmpty)
            .property("FirearmNeedsReload", &ACrab::FirearmNeedsReload)
            .property("FirearmIsSemiAuto", &ACrab::FirearmIsSemiAuto)
            .property("FirearmActivationDelay", &ACrab::FirearmActivationDelay)
            .def("ReloadFirearm", &ACrab::ReloadFirearm)
            .def("IsWithinRange", &ACrab::IsWithinRange)
            .def("Look", &ACrab::Look)
            .def("LookForMOs", &ACrab::LookForMOs)
            .def("IsOnScenePoint", &ACrab::IsOnScenePoint)
			.property("LimbPathPushForce", &ACrab::GetLimbPathPushForce, &ACrab::SetLimbPathPushForce)
			.def("GetLimbPathSpeed", &ACrab::GetLimbPathSpeed)
			.def("SetLimbPathSpeed", &ACrab::SetLimbPathSpeed),

        ABSTRACTLUABINDING(ACraft, Actor)
            .enum_("HatchState")
            [
                value("CLOSED", 0),
                value("OPENING", 1),
                value("OPEN", 2),
                value("CLOSING", 3),
                value("HatchStateCount", 4)
            ]
            .enum_("Side")
            [
                value("RIGHT", 0),
                value("LEFT", 1)
            ]
            .enum_("CraftDeliverySequence")
            [
                value("FALL", 0),
                value("LAND", 1),
                value("STANDBY", 2),
                value("UNLOAD", 3),
                value("LAUNCH", 4),
                value("UNSTICK", 5)
            ]
            .enum_("AltitudeMoveState")
            [
                value("HOVER", 0),
                value("DESCEND", 1),
                value("ASCEND", 2)
            ]
            .def("OpenHatch", &ACraft::OpenHatch)
            .def("CloseHatch", &ACraft::CloseHatch)
            .property("HatchState", &ACraft::GetHatchState)
            .property("MaxPassengers", &ACraft::GetMaxPassengers)
            .property("DeliveryDelayMultiplier", &ACraft::GetDeliveryDelayMultiplier),

        CONCRETELUABINDING(ACDropShip, ACraft)
            .property("RightEngine", &ACDropShip::GetRThruster)
            .property("LeftEngine", &ACDropShip::GetLThruster)
            .property("RightThruster", &ACDropShip::GetURThruster)
            .property("LeftThruster", &ACDropShip::GetULThruster)
			.property("LeftHatch", &ACDropShip::GetLHatch)
			.property("RightHatch", &ACDropShip::GetRHatch)
			.property("MaxEngineAngle", &ACDropShip::GetMaxEngineAngle, &ACDropShip::SetMaxEngineAngle)
			.property("LateralControlSpeed", &ACDropShip::GetLateralControlSpeed, &ACDropShip::SetLateralControlSpeed)
			.property("LateralControl", &ACDropShip::GetLateralControl)
			.def("DetectObstacle", &ACDropShip::DetectObstacle)
            .def("GetAltitude", &ACDropShip::GetAltitude),

        CONCRETELUABINDING(ACRocket, ACraft)
            .enum_("LandingGearState")
            [
                value("RAISED", 0),
                value("LOWERED", 1),
                value("LOWERING", 2),
                value("RAISING", 3),
                value("GearStateCount", 4)
            ]
			.property("MainEngine", &ACRocket::GetMThruster)
			.property("LeftEngine", &ACRocket::GetLThruster)
			.property("RightEngine", &ACRocket::GetRThruster)
			.property("LeftThruster", &ACRocket::GetULThruster)
			.property("RightThruster", &ACRocket::GetURThruster)
			.property("GearState", &ACRocket::GetGearState),

        CONCRETELUABINDING(HeldDevice, Attachable)
            .property("SupportPos", &HeldDevice::GetSupportPos)
            .property("MagazinePos", &HeldDevice::GetMagazinePos)
            .property("MuzzlePos", &HeldDevice::GetMuzzlePos)
            .property("MuzzleOffset", &HeldDevice::GetMuzzleOffset, &HeldDevice::SetMuzzleOffset)
            .property("StanceOffset", &HeldDevice::GetStanceOffset, &HeldDevice::SetStanceOffset)
            .property("SharpStanceOffset", &HeldDevice::GetSharpStanceOffset, &HeldDevice::SetSharpStanceOffset)
            .property("SharpLength", &HeldDevice::GetSharpLength, &HeldDevice::SetSharpLength)
            .def("IsWeapon", &HeldDevice::IsWeapon)
            .def("IsTool", &HeldDevice::IsTool)
            .def("IsShield", &HeldDevice::IsShield)
            .def("IsDualWieldable", &HeldDevice::IsDualWieldable)
            .def("SetDualWieldable", &HeldDevice::SetDualWieldable)
            .def("IsOneHanded", &HeldDevice::IsOneHanded)
            .def("SetOneHanded", &HeldDevice::SetOneHanded)
            .def("Activate", &HeldDevice::Activate)
            .def("Deactivate", &HeldDevice::Deactivate)
            .def("Reload", &HeldDevice::Reload)
            .def("IsActivated", &HeldDevice::IsActivated)
            .def("IsReloading", &HeldDevice::IsReloading)
            .def("DoneReloading", &HeldDevice::DoneReloading)
            .def("NeedsReloading", &HeldDevice::NeedsReloading)
            .def("IsFull", &HeldDevice::IsFull)
			.property("SharpLength", &HeldDevice::GetSharpLength, &HeldDevice::SetSharpLength)
			.property("SupportOffset", &HeldDevice::GetSupportOffset, &HeldDevice::SetSupportOffset)
			.def("SetSupported", &HeldDevice::SetSupported),

        CONCRETELUABINDING(Magazine, Attachable)
            .property("NextRound", &Magazine::GetNextRound)
            .property("RoundCount", &Magazine::GetRoundCount, &Magazine::SetRoundCount)
            .property("IsEmpty", &Magazine::IsEmpty)
            .property("IsFull", &Magazine::IsFull)
            .property("IsOverHalfFull", &Magazine::IsOverHalfFull)
            .property("Capacity", &Magazine::GetCapacity)
            .property("Discardable", &Magazine::IsDiscardable),

        CONCRETELUABINDING(Round, Entity)
            .property("NextParticle", &Round::GetNextParticle)
            .property("Shell", &Round::GetShell)
            .property("FireVel", &Round::GetFireVel)
            .property("ShellVel", &Round::GetShellVel)
            .property("Separation", &Round::GetSeparation)
            .property("ParticleCount", &Round::ParticleCount)
            .property("AILifeTime", &Round::GetAILifeTime)
            .property("AIFireVel", &Round::GetAIFireVel)
            .property("IsEmpty", &Round::IsEmpty),

        CONCRETELUABINDING(HDFirearm, HeldDevice)
            .property("RateOfFire", &HDFirearm::GetRateOfFire, &HDFirearm::SetRateOfFire)
			.property("FullAuto", &HDFirearm::IsFullAuto, &HDFirearm::SetFullAuto)
            .property("RoundInMagCount", &HDFirearm::GetRoundInMagCount)
            .property("Magazine", &HDFirearm::GetMagazine)
            .property("ActivationDelay", &HDFirearm::GetActivationDelay, &HDFirearm::SetActivationDelay)
            .property("DeactivationDelay", &HDFirearm::GetDeactivationDelay, &HDFirearm::SetDeactivationDelay)
            .property("ReloadTime", &HDFirearm::GetReloadTime, &HDFirearm::SetReloadTime)
            .property("ShakeRange", &HDFirearm::GetShakeRange, &HDFirearm::SetShakeRange)
            .property("SharpShakeRange", &HDFirearm::GetSharpShakeRange, &HDFirearm::SetSharpShakeRange)
            .property("NoSupportFactor", &HDFirearm::GetNoSupportFactor, &HDFirearm::SetNoSupportFactor)
            .property("ParticleSpreadRange", &HDFirearm::GetParticleSpreadRange, &HDFirearm::SetParticleSpreadRange)
			.property("FiredOnce", &HDFirearm::FiredOnce)
			.property("FiredFrame", &HDFirearm::FiredFrame)
			.property("RoundsFired", &HDFirearm::RoundsFired)
            .def("GetAIFireVel", &HDFirearm::GetAIFireVel)
            .def("GetAIBulletLifeTime", &HDFirearm::GetAIBulletLifeTime)
            .def("GetBulletAccScalar", &HDFirearm::GetBulletAccScalar)
            .def("GetAIBlastRadius", &HDFirearm::GetAIBlastRadius)
            .def("GetAIPenetration", &HDFirearm::GetAIPenetration)
            .def("CompareTrajectories", &HDFirearm::CompareTrajectories)
            .def("SetNextMagazineName", &HDFirearm::SetNextMagazineName)
			.property("IsAnimatedManually", &HDFirearm::IsAnimatedManually, &HDFirearm::SetAnimatedManually)
			.property("RecoilTransmission", &HDFirearm::GetRecoilTransmission, &HDFirearm::SetRecoilTransmission),

        CONCRETELUABINDING(ThrownDevice, HeldDevice)
            .property("MinThrowVel", &ThrownDevice::GetMinThrowVel, &ThrownDevice::SetMinThrowVel)
            .property("MaxThrowVel", &ThrownDevice::GetMaxThrowVel, &ThrownDevice::SetMaxThrowVel),

        CONCRETELUABINDING(TDExplosive, ThrownDevice)
            .property("IsAnimatedManually", &TDExplosive::IsAnimatedManually, &TDExplosive::SetAnimatedManually),

        class_<Controller>("Controller")
            .enum_("ControlState")
            [
                value("PRIMARY_ACTION", 0),
                value("SECONDARY_ACTION", 1),
                value("MOVE_IDLE", 2),
                value("MOVE_RIGHT", 3),
                value("MOVE_LEFT", 4),
                value("MOVE_UP", 5),
                value("MOVE_DOWN", 6),
                value("MOVE_FAST", 7),
                value("BODY_JUMPSTART", 8),
                value("BODY_JUMP", 9),
                value("BODY_CROUCH", 10),
                value("AIM_UP", 11),
                value("AIM_DOWN", 12),
                value("AIM_SHARP", 13),
                value("WEAPON_FIRE", 14),
                value("WEAPON_RELOAD", 15),
                value("PIE_MENU_ACTIVE", 16),
                value("WEAPON_CHANGE_NEXT", 17),
                value("WEAPON_CHANGE_PREV", 18),
                value("WEAPON_PICKUP", 19),
                value("WEAPON_DROP", 20),
                value("ACTOR_NEXT", 21),
                value("ACTOR_PREV", 22),
                value("ACTOR_BRAIN", 23),
                value("ACTOR_NEXT_PREP", 24),
                value("ACTOR_PREV_PREP", 25),
                value("HOLD_RIGHT", 26),
                value("HOLD_LEFT", 27),
                value("HOLD_UP", 28),
                value("HOLD_DOWN", 29),
                value("PRESS_PRIMARY", 30),
                value("PRESS_SECONDARY", 31),
                value("PRESS_RIGHT", 32),
                value("PRESS_LEFT", 33),
                value("PRESS_UP", 34),
                value("PRESS_DOWN", 35),
                value("RELEASE_PRIMARY", 36),
                value("RELEASE_SECONDARY", 37),
                value("PRESS_FACEBUTTON", 38),
                value("SCROLL_UP", 39),
                value("SCROLL_DOWN", 40),
                value("DEBUG_ONE", 41),
                value("CONTROLSTATECOUNT", 42)
            ]
            .enum_("InputMode")
            [
                value("CIM_DISABLED", 0),
                value("CIM_PLAYER", 1),
                value("CIM_AI", 2),
                value("CIM_NETWORK", 3),
                value("CIM_INPUTMODECOUNT", 4)
            ]
            .def(luabind::constructor<>())
            .property("InputMode", &Controller::GetInputMode, &Controller::SetInputMode)
            .def("IsPlayerControlled", &Controller::IsPlayerControlled)
            .property("ControlledActor", &Controller::GetControlledActor, &Controller::SetControlledActor)
            .property("Team", &Controller::GetTeam, &Controller::SetTeam)
            .property("AnalogMove", &Controller::GetAnalogMove, &Controller::SetAnalogMove)
            .property("AnalogAim", &Controller::GetAnalogAim, &Controller::SetAnalogAim)
            .property("AnalogCursor", &Controller::GetAnalogCursor)
            .def("RelativeCursorMovement", &Controller::RelativeCursorMovement)
            .property("Player", &Controller::GetPlayer, &Controller::SetPlayer)
            .def("IsMouseControlled", &Controller::IsMouseControlled)
            .property("MouseMovement", &Controller::GetMouseMovement)
            .property("Disabled", &Controller::IsDisabled, &Controller::SetDisabled)
            .def("SetState", &Controller::SetState)
            .def("IsState", &Controller::IsState),

        class_<Timer>("Timer")
            .def(luabind::constructor<>())
            .def("Reset", &Timer::Reset)
            .property("StartRealTimeMS", &Timer::GetStartRealTimeMS, &Timer::SetStartRealTimeMS)
            .property("ElapsedRealTimeS", &Timer::GetElapsedRealTimeS, &Timer::SetElapsedRealTimeS)
            .property("ElapsedRealTimeMS", &Timer::GetElapsedRealTimeMS, &Timer::SetElapsedRealTimeMS)
            .def("SetRealTimeLimitMS", &Timer::SetRealTimeLimitMS)
            .def("SetRealTimeLimitS", &Timer::SetRealTimeLimitS)
            .def("IsPastRealTimeLimit", &Timer::IsPastRealTimeLimit)
            .def("LeftTillRealTimeLimitMS", &Timer::LeftTillRealTimeLimitMS)
            .def("LeftTillRealTimeLimitS", &Timer::LeftTillRealTimeLimitS)
            .def("LeftTillRealMS", &Timer::LeftTillRealMS)
            .def("IsPastRealMS", &Timer::IsPastRealMS)
            .def("AlternateReal", &Timer::AlternateReal)
            .property("StartSimTimeMS", &Timer::GetStartSimTimeMS, &Timer::SetStartSimTimeMS)
            .property("ElapsedSimTimeS", &Timer::GetElapsedSimTimeS, &Timer::SetElapsedSimTimeS)
            .property("ElapsedSimTimeMS", &Timer::GetElapsedSimTimeMS, &Timer::SetElapsedSimTimeMS)
            .def("SetSimTimeLimitMS", &Timer::SetSimTimeLimitMS)
            .def("SetSimTimeLimitS", &Timer::SetSimTimeLimitS)
            .def("IsPastSimTimeLimit", &Timer::IsPastSimTimeLimit)
            .def("LeftTillSimTimeLimitMS", &Timer::LeftTillSimTimeLimitMS)
            .def("LeftTillSimTimeLimitS", &Timer::LeftTillSimTimeLimitS)
            .def("LeftTillSimMS", &Timer::LeftTillSimMS)
            .def("IsPastSimMS", &Timer::IsPastSimMS)
            .def("AlternateSim", &Timer::AlternateSim),

        class_<TimerMan>("TimerManager")
            .property("TicksPerSecond", &TimerMan::GetTicksPerSecondInLua)
            .property("TimeScale", &TimerMan::GetTimeScale, &TimerMan::SetTimeScale)
            .property("RealToSimCap", &TimerMan::GetRealToSimCap, &TimerMan::SetRealToSimCap)
            .property("DeltaTimeTicks", &TimerMan::GetDeltaTimeTicks, &TimerMan::SetDeltaTimeTicks)
            .property("DeltaTimeSecs", &TimerMan::GetDeltaTimeSecs, &TimerMan::SetDeltaTimeSecs)
            .property("DeltaTimeMS", &TimerMan::GetDeltaTimeMS)
            //.def("PauseSim", &TimerMan::PauseSim) // Forcing this during activity will kill input and your only option will be to Alt+F4. Need to rework input so it's not tied to sim time for this to work.
            .property("OneSimUpdatePerFrame", &TimerMan::IsOneSimUpdatePerFrame, &TimerMan::SetOneSimUpdatePerFrame)
            .def("TimeForSimUpdate", &TimerMan::TimeForSimUpdate)
            .def("DrawnSimUpdate", &TimerMan::DrawnSimUpdate),

        class_<FrameMan>("FrameManager")
            .property("PlayerScreenWidth", &FrameMan::GetPlayerScreenWidth)
            .property("PlayerScreenHeight", &FrameMan::GetPlayerScreenHeight)
			.def("LoadPalette", &FrameMan::LoadPalette)
            .def("SetScreenText", &FrameMan::SetScreenText)
            .def("ClearScreenText", &FrameMan::ClearScreenText)
            .def("FadeInPalette", &FrameMan::FadeInPalette)
            .def("FadeOutPalette", &FrameMan::FadeOutPalette)
            .def("SaveScreenToBMP", &FrameMan::SaveScreenToBMP)
            .def("SaveBitmapToBMP", &FrameMan::SaveBitmapToBMP)
            .def("FlashScreen", &FrameMan::FlashScreen)
			.def("CalculateTextHeight", &FrameMan::CalculateTextHeight)
			.def("CalculateTextWidth", &FrameMan::CalculateTextWidth),

		class_<PostProcessMan>("PostProcessManager")
			.def("RegisterPostEffect", &PostProcessMan::RegisterPostEffect),

		class_<PrimitiveMan>("PrimitiveManager")
			.def("DrawLinePrimitive", (void (PrimitiveMan::*)(Vector start, Vector end, unsigned char color))&PrimitiveMan::DrawLinePrimitive)
			.def("DrawLinePrimitive", (void (PrimitiveMan::*)(short player, Vector start, Vector end, unsigned char color))&PrimitiveMan::DrawLinePrimitive)
			.def("DrawBoxPrimitive", (void (PrimitiveMan::*)(Vector start, Vector end, unsigned char color))&PrimitiveMan::DrawBoxPrimitive)
			.def("DrawBoxPrimitive", (void (PrimitiveMan::*)(short player, Vector start, Vector end, unsigned char color))&PrimitiveMan::DrawBoxPrimitive)
			.def("DrawBoxFillPrimitive", (void (PrimitiveMan::*)(Vector start, Vector end, unsigned char color))&PrimitiveMan::DrawBoxFillPrimitive)
			.def("DrawBoxFillPrimitive", (void (PrimitiveMan::*)(short player, Vector start, Vector end, unsigned char color))&PrimitiveMan::DrawBoxFillPrimitive)
			.def("DrawCirclePrimitive", (void (PrimitiveMan::*)(Vector pos, short radius, unsigned char color))&PrimitiveMan::DrawCirclePrimitive)
			.def("DrawCirclePrimitive", (void (PrimitiveMan::*)(short player, Vector pos, short radius, unsigned char color))&PrimitiveMan::DrawCirclePrimitive)
			.def("DrawCircleFillPrimitive", (void (PrimitiveMan::*)(Vector pos, short radius, unsigned char color))&PrimitiveMan::DrawCircleFillPrimitive)
			.def("DrawCircleFillPrimitive", (void (PrimitiveMan::*)(short player, Vector pos, short radius, unsigned char color))&PrimitiveMan::DrawCircleFillPrimitive)
			.def("DrawTextPrimitive", (void (PrimitiveMan::*)(Vector start, std::string text, bool isSmall, short alignment))&PrimitiveMan::DrawTextPrimitive)
			.def("DrawTextPrimitive", (void (PrimitiveMan::*)(short player, Vector start, std::string text, bool isSmall, short alignment))&PrimitiveMan::DrawTextPrimitive)
			.def("DrawBitmapPrimitive", (void (PrimitiveMan::*)(Vector start, Entity * pEntity, float rotAngle, unsigned short frame))&PrimitiveMan::DrawBitmapPrimitive)
			.def("DrawBitmapPrimitive", (void (PrimitiveMan::*)(short player, Vector start, Entity * pEntity, float rotAngle, unsigned short frame))&PrimitiveMan::DrawBitmapPrimitive),

        class_<PresetMan>("PresetManager")
            .def("LoadDataModule", (bool (PresetMan::*)(string))&PresetMan::LoadDataModule)
            .def("GetDataModule", &PresetMan::GetDataModule)
            .def("GetModuleID", &PresetMan::GetModuleID)
            .def("GetModuleIDFromPath", &PresetMan::GetModuleIDFromPath)
            .def("GetTotalModuleCount", &PresetMan::GetTotalModuleCount)
            .def("GetOfficialModuleCount", &PresetMan::GetOfficialModuleCount)
            .def("AddPreset", &PresetMan::AddEntityPreset)
			.def_readwrite("Modules", &PresetMan::m_pDataModules, return_stl_iterator)
            .def("GetPreset", (const Entity *(PresetMan::*)(string, string, int))&PresetMan::GetEntityPreset)
            .def("GetPreset", (const Entity *(PresetMan::*)(string, string, string))&PresetMan::GetEntityPreset)
            .def("GetLoadout", (Actor * (PresetMan::*)(std::string, std::string, bool))&PresetMan::GetLoadout, adopt(result))
            .def("GetLoadout", (Actor * (PresetMan::*)(std::string, int, bool))&PresetMan::GetLoadout, adopt(result))
            .def("GetRandomOfGroup", &PresetMan::GetRandomOfGroup)
            .def("GetRandomOfGroupInModuleSpace", &PresetMan::GetRandomOfGroupInModuleSpace)
            .def("GetEntityDataLocation", &PresetMan::GetEntityDataLocation)
            .def("ReadReflectedPreset", &PresetMan::ReadReflectedPreset)
            .def("ReloadAllScripts", &PresetMan::ReloadAllScripts),

        class_<AudioMan>("AudioManager")
            .def("GetGlobalPitch", &AudioMan::GetGlobalPitch)
            .def("IsMusicPlaying", &AudioMan::IsMusicPlaying)
            .property("MusicVolume", &AudioMan::GetMusicVolume, &AudioMan::SetMusicVolume)
            .def("SetTempMusicVolume", &AudioMan::SetTempMusicVolume)
            .def("GetMusicPosition", &AudioMan::GetMusicPosition)
            .def("SetMusicPosition", &AudioMan::SetMusicPosition)
            .def("SetMusicPitch", &AudioMan::SetMusicPitch)
            .property("SoundsVolume", &AudioMan::GetSoundsVolume, &AudioMan::SetSoundsVolume)
            .def("SetSoundPosition", &AudioMan::SetSoundPosition)
            .def("SetSoundPitch", &AudioMan::SetSoundPitch)
            .def("StopAll", &AudioMan::StopMusic)
            .def("PlayMusic", &AudioMan::PlayMusic)
            .def("PlayNextStream", &AudioMan::PlayNextStream)
            .def("StopMusic", &AudioMan::StopMusic)
            .def("QueueMusicStream", &AudioMan::QueueMusicStream)
            .def("QueueSilence", &AudioMan::QueueSilence)
            .def("ClearMusicQueue", &AudioMan::ClearMusicQueue)
            .def("PlaySound", (SoundContainer *(AudioMan:: *)(const char *filePath)) &AudioMan::PlaySound)
            .def("PlaySound", (SoundContainer *(AudioMan:: *)(const char *filePath, const Vector &position)) &AudioMan::PlaySound)
            .def("PlaySound", (SoundContainer *(AudioMan:: *)(const char *filePath, const Vector &position, int player)) &AudioMan::PlaySound)
            .def("PlaySound", (SoundContainer *(AudioMan:: *)(const char *filePath, const Vector &position, int player, int loops, int priority, double pitchOrAffectedByGlobalPitch, float attenuationStartDistance, bool immobile)) &AudioMan::PlaySound)
            .def("StopSound", (bool (AudioMan:: *)(SoundContainer *soundContainer)) &AudioMan::StopSound)
            .def("StopSound", (bool (AudioMan:: *)(SoundContainer *soundContainer, int player)) &AudioMan::StopSound)
            .def("FadeOutSound", &AudioMan::FadeOutSound),

        class_<UInputMan>("UInputManager")
			.def("GetInputDevice", &UInputMan::GetInputDevice)
            .def("ElementPressed", &UInputMan::ElementPressed)
            .def("ElementReleased", &UInputMan::ElementReleased)
            .def("ElementHeld", &UInputMan::ElementHeld)
            .def("KeyPressed", &UInputMan::KeyPressed)
            .def("KeyReleased", &UInputMan::KeyReleased)
            .def("KeyHeld", &UInputMan::KeyHeld)
            .def("WhichKeyHeld", &UInputMan::WhichKeyHeld)
            .def("MouseButtonPressed", (bool (UInputMan::*)(int,short))&UInputMan::MouseButtonPressed) 
			.def("MouseButtonPressed", (bool (UInputMan::*)(int))&UInputMan::MouseButtonPressed) 
			.def("MouseButtonReleased", (bool (UInputMan::*)(int, short))&UInputMan::MouseButtonReleased)
			.def("MouseButtonReleased", (bool (UInputMan::*)(int))&UInputMan::MouseButtonReleased)
			.def("MouseButtonHeld", (bool (UInputMan::*)(int, short))&UInputMan::MouseButtonHeld)
			.def("MouseButtonHeld", (bool (UInputMan::*)(int))&UInputMan::MouseButtonHeld)
			.def("MouseWheelMoved", &UInputMan::MouseWheelMoved)
            .def("JoyButtonPressed", &UInputMan::JoyButtonPressed)
            .def("JoyButtonReleased", &UInputMan::JoyButtonReleased)
            .def("JoyButtonHeld", &UInputMan::JoyButtonHeld)
            .def("WhichJoyButtonPressed", &UInputMan::WhichJoyButtonPressed)
            .def("JoyDirectionPressed", &UInputMan::JoyDirectionPressed)
            .def("JoyDirectionReleased", &UInputMan::JoyDirectionReleased)
            .def("JoyDirectionHeld", &UInputMan::JoyDirectionHeld)
            .def("AnalogMoveValues", &UInputMan::AnalogMoveValues)
            .def("AnalogAimValues", &UInputMan::AnalogAimValues)
            .def("SetMouseValueMagnitude", &UInputMan::SetMouseValueMagnitude)
            .def("AnalogAxisValue", &UInputMan::AnalogAxisValue)
            .def("AnalogStickValues", &UInputMan::AnalogStickValues)
            .def("MouseUsedByPlayer", &UInputMan::MouseUsedByPlayer)
            .def("AnyMouseButtonPress", &UInputMan::AnyMouseButtonPress)
            .def("GetMouseMovement", &UInputMan::GetMouseMovement)
            .def("DisableMouseMoving", &UInputMan::DisableMouseMoving)
            .def("SetMousePos", &UInputMan::SetMousePos)
            .def("ForceMouseWithinBox", &UInputMan::ForceMouseWithinBox)
            .def("AnyJoyInput", &UInputMan::AnyJoyInput)
            .def("AnyJoyPress", &UInputMan::AnyJoyPress)
            .def("AnyJoyButtonPress", &UInputMan::AnyJoyButtonPress)
            .def("AnyInput", &UInputMan::AnyInput)
            .def("AnyPress", &UInputMan::AnyPress)
            .def("AnyStartPress", &UInputMan::AnyStartPress)
            .property("FlagAltState", &UInputMan::FlagAltState)
            .property("FlagCtrlState", &UInputMan::FlagCtrlState)
            .property("FlagShiftState", &UInputMan::FlagShiftState),

        class_<IntRect>("IntRect")
            .def(luabind::constructor<>())
            .def(luabind::constructor<int, int, int, int>())
            .def_readwrite("Left", &IntRect::m_Left)
            .def_readwrite("Top", &IntRect::m_Top)
            .def_readwrite("Right", &IntRect::m_Right)
            .def_readwrite("Bottom", &IntRect::m_Bottom),

        CONCRETELUABINDING(Scene, Entity)
            .enum_("PlacedObjectSets")
            [
                value("PLACEONLOAD", 0),
                value("BLUEPRINT", 1),
                value("AIPLAN", 2),
                value("PLACEDSETSCOUNT", 3)
            ]
            .property("Location", &Scene::GetLocation, &Scene::SetLocation)
//            .property("Terrain", &Scene::GetTerrain)
            .property("Dimensions", &Scene::GetDimensions)
            .property("Width", &Scene::GetWidth)
            .property("Height", &Scene::GetHeight)
            .property("WrapsX", &Scene::WrapsX)
            .property("WrapsY", &Scene::WrapsY)
            .property("TeamOwnership", &Scene::GetTeamOwnership, &Scene::SetTeamOwnership)
            .def("GetBuildBudget", &Scene::GetBuildBudget)
            .def("SetBuildBudget", &Scene::SetBuildBudget)
            .def("IsScanScheduled", &Scene::IsScanScheduled)
            .def("SetScheduledScan", &Scene::SetScheduledScan)
            .def("ClearPlacedObjectSet", &Scene::ClearPlacedObjectSet)
            .def("PlaceResidentBrain", &Scene::PlaceResidentBrain)
            .def("PlaceResidentBrains", &Scene::PlaceResidentBrains)
            .def("RetrieveResidentBrains", &Scene::RetrieveResidentBrains)
            .def("GetResidentBrain", &Scene::GetResidentBrain)
            .def("SetResidentBrain", &Scene::SetResidentBrain)
            .def("SetArea", &Scene::SetArea)
            .def("HasArea", &Scene::HasArea)
            .def("GetArea", &Scene::GetArea)
			.def("GetOptionalArea", &Scene::GetOptionalArea)
			.def("WithinArea", &Scene::WithinArea)
            .property("GlobalAcc", &Scene::GetGlobalAcc, &Scene::SetGlobalAcc)
			.property("GlocalAcc", &Scene::GetGlobalAcc, &Scene::SetGlobalAcc)
			.def("ResetPathFinding", &Scene::ResetPathFinding)
            .def("UpdatePathFinding", &Scene::UpdatePathFinding)
            .def("PathFindingUpdated", &Scene::PathFindingUpdated)
            .def("CalculatePath", &Scene::CalculateScenePath)
            .def_readwrite("ScenePath", &Scene::m_ScenePath, return_stl_iterator)
			.def_readwrite("Deployments", &Scene::m_Deployments, return_stl_iterator)
			.property("ScenePathSize", &Scene::GetScenePathSize),

		ABSTRACTLUABINDING(Deployment, SceneObject)
			.def("CreateDeployedActor", (Actor * (Deployment::*)())&Deployment::CreateDeployedActor, adopt(result))
			.def("CreateDeployedObject", (SceneObject * (Deployment::*)())&Deployment::CreateDeployedObject, adopt(result))
			.def("GetLoadoutName", &Deployment::GetLoadoutName)
			.property("SpawnRadius", &Deployment::GetSpawnRadius)
			.property("ID", &Deployment::GetID)
			.property("HFlipped", &Deployment::IsHFlipped),

        class_<SceneMan>("SceneManager")
            .property("Scene", &SceneMan::GetScene)
            .def("LoadScene", (int (SceneMan::*)(string, bool, bool))&SceneMan::LoadScene)
            .def("LoadScene", (int (SceneMan::*)(string, bool))&SceneMan::LoadScene)
            .property("SceneDim", &SceneMan::GetSceneDim)
            .property("SceneWidth", &SceneMan::GetSceneWidth)
            .property("SceneHeight", &SceneMan::GetSceneHeight)
            .property("SceneWrapsX", &SceneMan::SceneWrapsX)
            .property("SceneWrapsY", &SceneMan::SceneWrapsY)
            .def("GetOffset", &SceneMan::GetOffset)
            .def("SetOffset", (void (SceneMan::*)(const Vector &, int))&SceneMan::SetOffset)
            .def("SetOffsetX", &SceneMan::SetOffsetX)
            .def("SetOffsetY", &SceneMan::SetOffsetY)
            .def("GetScreenOcclusion", &SceneMan::GetScreenOcclusion)
            .def("SetScreenOcclusion", &SceneMan::SetScreenOcclusion)
            .def("GetTerrain", &SceneMan::GetTerrain)
            .def("GetMaterial", &SceneMan::GetMaterial)
            .def("GetMaterialFromID", &SceneMan::GetMaterialFromID)
            .property("LayerDrawMode", &SceneMan::GetLayerDrawMode, &SceneMan::SetLayerDrawMode)
            .def("GetTerrMatter", &SceneMan::GetTerrMatter)
            .def("GetMOIDPixel", &SceneMan::GetMOIDPixel)
            .property("GlobalAcc", &SceneMan::GetGlobalAcc)
            .property("OzPerKg", &SceneMan::GetOzPerKg)
            .property("KgPerOz", &SceneMan::GetKgPerOz)
            .def("SetLayerDrawMode", &SceneMan::SetLayerDrawMode)
            .def("SetScroll", &SceneMan::SetScroll)
            .def("SetScrollTarget", &SceneMan::SetScrollTarget)
            .def("GetScrollTarget", &SceneMan::GetScrollTarget)
            .def("TargetDistanceScalar", &SceneMan::TargetDistanceScalar)
            .def("CheckOffset", &SceneMan::CheckOffset)
            .def("LoadUnseenLayer", &SceneMan::LoadUnseenLayer)
            .def("MakeAllUnseen", &SceneMan::MakeAllUnseen)
            .def("AnythingUnseen", &SceneMan::AnythingUnseen)
            .def("GetUnseenResolution", &SceneMan::GetUnseenResolution)
            .def("IsUnseen", &SceneMan::IsUnseen)
            .def("RevealUnseen", &SceneMan::RevealUnseen)
            .def("RevealUnseenBox", &SceneMan::RevealUnseenBox)
            .def("RestoreUnseen", &SceneMan::RestoreUnseen)
            .def("RestoreUnseenBox", &SceneMan::RestoreUnseenBox)
			.def("CastSeeRay", &SceneMan::CastSeeRay)
			.def("CastUnseeRay", &SceneMan::CastUnseeRay)
			.def("CastUnseenRay", &SceneMan::CastUnseenRay)
			.def("CastMaterialRay", (bool (SceneMan::*)(const Vector &, const Vector &, unsigned char, Vector &, int, bool))&SceneMan::CastMaterialRay)
            .def("CastMaterialRay", (float (SceneMan::*)(const Vector &, const Vector &, unsigned char, int))&SceneMan::CastMaterialRay)
            .def("CastNotMaterialRay", (bool (SceneMan::*)(const Vector &, const Vector &, unsigned char, Vector &, int, bool))&SceneMan::CastNotMaterialRay)
            .def("CastNotMaterialRay", (float (SceneMan::*)(const Vector &, const Vector &, unsigned char, int, bool))&SceneMan::CastNotMaterialRay)
            .def("CastStrengthSumRay", &SceneMan::CastStrengthSumRay)
            .def("CastMaxStrengthRay", &SceneMan::CastMaxStrengthRay)
            .def("CastStrengthRay", &SceneMan::CastStrengthRay)
            .def("CastWeaknessRay", &SceneMan::CastWeaknessRay)
            .def("CastMORay", &SceneMan::CastMORay)
            .def("CastFindMORay", &SceneMan::CastFindMORay)
            .def("CastObstacleRay", &SceneMan::CastObstacleRay)
            .def("GetLastRayHitPos", &SceneMan::GetLastRayHitPos)
            .def("FindAltitude", &SceneMan::FindAltitude)
            .def("MovePointToGround", &SceneMan::MovePointToGround)
            .def("IsWithinBounds", &SceneMan::IsWithinBounds)
            .def("ForceBounds", (bool (SceneMan::*)(int &, int &))&SceneMan::ForceBounds)
            .def("ForceBounds", (bool (SceneMan::*)(Vector &))&SceneMan::ForceBounds)//, out_value(_2))
            .def("WrapPosition", (bool (SceneMan::*)(int &, int &))&SceneMan::WrapPosition)
            .def("WrapPosition", (bool (SceneMan::*)(Vector &))&SceneMan::WrapPosition)//, out_value(_2))
            .def("SnapPosition", &SceneMan::SnapPosition)
            .def("ShortestDistance", &SceneMan::ShortestDistance)
            .def("ObscuredPoint", (bool (SceneMan::*)(Vector &, int))&SceneMan::ObscuredPoint)//, out_value(_2))
            .def("ObscuredPoint", (bool (SceneMan::*)(int, int, int))&SceneMan::ObscuredPoint)
            .def("AddSceneObject", &SceneMan::AddSceneObject)
            .def("AddTerrainObject", &SceneMan::AddTerrainObject)
			.def("CheckAndRemoveOrphans", (int (SceneMan::*)(int, int, int, int, bool))&SceneMan::RemoveOrphans),

		class_<DataModule>("DataModule")
			.def_readwrite("Presets", &DataModule::m_EntityList, return_stl_iterator)
			.property("FileName", &DataModule::GetFileName)
			.property("FriendlyName", &DataModule::GetFriendlyName),

        class_<BuyMenuGUI>("BuyMenuGUI")
            .def("SetMetaPlayer", &BuyMenuGUI::SetMetaPlayer)
            .def("SetNativeTechModule", &BuyMenuGUI::SetNativeTechModule)
            .def("SetForeignCostMultiplier", &BuyMenuGUI::SetForeignCostMultiplier)
            .def("SetModuleExpanded", &BuyMenuGUI::SetModuleExpanded)
            .def("LoadAllLoadoutsFromFile", &BuyMenuGUI::LoadAllLoadoutsFromFile)
            .def("AddAllowedItem", &BuyMenuGUI::AddAllowedItem)
			.def("RemoveAllowedItem", &BuyMenuGUI::RemoveAllowedItem)
			.def("ClearAllowedItems", &BuyMenuGUI::ClearAllowedItems)
			.def("AddAlwaysAllowedItem", &BuyMenuGUI::AddAlwaysAllowedItem)
			.def("RemoveAlwaysAllowedItem", &BuyMenuGUI::RemoveAlwaysAllowedItem)
			.def("ClearAlwaysAllowedItems", &BuyMenuGUI::ClearAlwaysAllowedItems)
			.def("AddProhibitedItem", &BuyMenuGUI::AddProhibitedItem)
			.def("RemoveProhibitedItem", &BuyMenuGUI::RemoveProhibitedItem)
			.def("ClearProhibitedItems", &BuyMenuGUI::ClearProhibitedItems)
			.def("ForceRefresh", &BuyMenuGUI::ForceRefresh)
			.def("SetOwnedItemsAmount", &BuyMenuGUI::SetOwnedItemsAmount)
			.def("GetOwnedItemsAmount", &BuyMenuGUI::GetOwnedItemsAmount)
			.def("SetHeaderImage", &BuyMenuGUI::SetHeaderImage)
			.def("SetLogoImage", &BuyMenuGUI::SetLogoImage)
			.def("ClearCartList", &BuyMenuGUI::ClearCartList)
			.def("LoadDefaultLoadoutToCart", &BuyMenuGUI::LoadDefaultLoadoutToCart)
			.property("ShowOnlyOwnedItems", &BuyMenuGUI::GetOnlyShowOwnedItems, &BuyMenuGUI::SetOnlyShowOwnedItems)
			.property("EnforceMaxPassengersConstraint", &BuyMenuGUI::EnforceMaxPassengersConstraint, &BuyMenuGUI::SetEnforceMaxPassengersConstraint)
			.property("EnforceMaxMassConstraint", &BuyMenuGUI::EnforceMaxMassConstraint, &BuyMenuGUI::SetEnforceMaxMassConstraint),

        class_<SceneEditorGUI>("SceneEditorGUI")
            .enum_("EditorGUIMode")
            [
                value("INACTIVE", 0),
                value("PICKINGOBJECT", 1),
                value("ADDINGOBJECT", 2),
                value("INSTALLINGBRAIN", 3),
                value("PLACINGOBJECT", 4),
                value("MOVINGOBJECT", 5),
                value("DELETINGOBJECT", 6),
                value("PLACEINFRONT", 7),
                value("PLACEBEHIND", 8),
                value("DONEEDITING", 9),
                value("EDITORGUIMODECOUNT", 10)
            ]
            .def("SetCursorPos", &SceneEditorGUI::SetCursorPos)
            .property("EditorMode", &SceneEditorGUI::GetEditorGUIMode, &SceneEditorGUI::SetEditorGUIMode)
            .def("GetCurrentObject", &SceneEditorGUI::GetCurrentObject)
            .def("SetCurrentObject", &SceneEditorGUI::SetCurrentObject)
            .def("SetModuleSpace", &SceneEditorGUI::SetModuleSpace)
            .def("SetNativeTechModule", &SceneEditorGUI::SetNativeTechModule)
            .def("SetForeignCostMultiplier", &SceneEditorGUI::SetForeignCostMultiplier)
            .def("TestBrainResidence", &SceneEditorGUI::TestBrainResidence),

        class_<Activity, Entity>("Activity")
			.enum_("Players")[
				value("PLAYER_NONE", -1),
				value("PLAYER_1", 0),
				value("PLAYER_2", 1),
				value("PLAYER_3", 2),
				value("PLAYER_4", 3),
				value("MAXPLAYERCOUNT", 4)
			]

            .enum_("ActivityState")
            [
                value("NOACTIVITY", -1),
                value("NOTSTARTED", 0),
                value("STARTING", 1),
                value("EDITING", 2),
                value("PREGAME", 3),
                value("RUNNING", 4),
                value("INERROR", 6),
                value("OVER", 7)
            ]
            .enum_("Team")
            [
                value("NOTEAM", -1),
                value("TEAM_1", 0),
                value("TEAM_2", 1),
                value("TEAM_3", 2),
                value("TEAM_4", 3),
                value("MAXTEAMCOUNT", 4)
            ]
            .enum_("ViewState")
            [
                value("NORMAL", 0),
                value("OBSERVE", 1),
                value("DEATHWATCH", 2),
                value("ACTORSELECT", 3),
                value("AISENTRYPOINT", 4),
                value("AIPATROLPOINTS", 5),
                value("AIGOLDDIGPOINT", 6),
                value("AIGOTOPOINT", 7),
                value("LZSELECT", 8)
            ]
            .enum_("DifficultySetting")
            [
                value("MINDIFFICULTY", 0),
                value("CAKEDIFFICULTY", 15),
                value("EASYDIFFICULTY", 40),
                value("MEDIUMDIFFICULTY", 60),
                value("HARDDIFFICULTY", 85),
                value("NUTSDIFFICULTY", 98),
                value("MAXDIFFICULTY", 100)
            ]
            .enum_("AISkillSetting")
            [
				value("INFERIORSKILL", Activity::AISkillSetting::InferiorSkill),
				value("DEFAULTSKILL", Activity::AISkillSetting::DefaultSkill),
				value("AVERAGESKILL", Activity::AISkillSetting::AverageSkill),
				value("GOODSKILL", Activity::AISkillSetting::GoodSkill),
				value("UNFAIRSKILL", Activity::AISkillSetting::UnfairSkill)	
            ]
            .def(constructor<>())
            .property("ClassName", &Activity::GetClassName)
            .property("Description", &Activity::GetDescription)
            .property("InCampaignStage", &Activity::GetInCampaignStage, &Activity::SetInCampaignStage)
            .property("ActivityState", &Activity::GetActivityState, &Activity::SetActivityState)
            .property("SceneName", &Activity::GetSceneName, &Activity::SetSceneName)
            .property("PlayerCount", &Activity::GetPlayerCount)
            .def("DeactivatePlayer", &Activity::DeactivatePlayer)
            .def("PlayerActive", &Activity::PlayerActive)
            .def("PlayerHuman", &Activity::PlayerHuman)
            .property("HumanCount", &Activity::GetHumanCount)
            .property("TeamCount", &Activity::GetTeamCount)
            .def("TeamActive", &Activity::TeamActive)
            .def("GetTeamOfPlayer", &Activity::GetTeamOfPlayer)
            .def("SetTeamOfPlayer", &Activity::SetTeamOfPlayer)
            .def("PlayersInTeamCount", &Activity::PlayersInTeamCount)
            .def("ScreenOfPlayer", &Activity::ScreenOfPlayer)
            .def("GetViewState", &Activity::GetViewState)
            .def("SetViewState", &Activity::SetViewState)
            .def("GetPlayerBrain", &Activity::GetPlayerBrain)
            .def("SetPlayerBrain", &Activity::SetPlayerBrain)
            .def("PlayerHadBrain", &Activity::PlayerHadBrain)
            .def("SetBrainEvacuated", &Activity::SetBrainEvacuated)
            .def("BrainWasEvacuated", &Activity::BrainWasEvacuated)
            .def("IsAssignedBrain", &Activity::IsAssignedBrain)
            .def("IsBrainOfWhichPlayer", &Activity::IsBrainOfWhichPlayer)
            .def("IsOtherPlayerBrain", &Activity::IsOtherPlayerBrain)
            .def("HumanBrainCount", &Activity::HumanBrainCount)
            .def("AIBrainCount", &Activity::AIBrainCount)
            .def("GetControlledActor", &Activity::GetControlledActor)
            .def("SetTeamFunds", &Activity::SetTeamFunds)
            .def("GetTeamFunds", &Activity::GetTeamFunds)
            .def("SetTeamAISkill", &Activity::SetTeamAISkill)
            .def("GetTeamAISkill", &Activity::GetTeamAISkill)
            .def("ChangeTeamFunds", &Activity::ChangeTeamFunds)
            .def("TeamFundsChanged", &Activity::TeamFundsChanged)
            .def("ReportDeath", &Activity::ReportDeath)
            .def("GetTeamDeathCount", &Activity::GetTeamDeathCount)
            .def("Running", &Activity::IsRunning)
            .def("Paused", &Activity::IsPaused)
            .def("ActivityOver", &Activity::IsOver)
            .def("EnteredOrbit", &Activity::EnteredOrbit)
            .def("SwitchToActor", &Activity::SwitchToActor)
            .def("SwitchToNextActor", &Activity::SwitchToNextActor)
            .def("SwitchToPrevActor", &Activity::SwitchToPrevActor)
            .property("Difficulty", &Activity::GetDifficulty, &Activity::SetDifficulty)
            .def("IsPlayerTeam", &Activity::IsPlayerTeam)
            .def("ResetMessageTimer", &Activity::ResetMessageTimer)
// These are defined later in GAScripted
/*            .def("Start", &Activity::Start)
            .def("Pause", &Activity::Pause)
            .def("End", &Activity::End)*/,

        class_<GUIBanner>("GUIBanner")
            .enum_("AnimMode")
            [
                value("BLINKING", 0),
                value("FLYBYLEFTWARD", 1),
                value("FLYBYRIGHTWARD", 2),
                value("ANIMMODECOUNT", 3)
            ]
            .enum_("AnimState")
            [
                value("NOTSTARTED", 0),
                value("SHOWING", 1),
                value("SHOW", 2),
                value("HIDING", 3),
                value("OVER", 4),
                value("ANIMSTATECOUNT", 5)
            ]
            .enum_("BannerColor")
            [
                value("RED", 0),
                value("YELLOW", 1)
            ]
            .property("BannerText", &GUIBanner::GetBannerText)
            .property("AnimState", &GUIBanner::GetAnimState)
            .def("IsVisible", &GUIBanner::IsVisible)
            .property("Kerning", &GUIBanner::GetKerning, &GUIBanner::SetKerning)
            .def("ShowText", &GUIBanner::ShowText)
            .def("HideText", &GUIBanner::HideText)
            .def("ClearText", &GUIBanner::ClearText),

        def("ToGameActivity", (GameActivity *(*)(Entity *))&ToGameActivity),
        def("ToGameActivity", (const GameActivity *(*)(const Entity *))&ToConstGameActivity),
        class_<GameActivity, Activity>("GameActivity")
            .enum_("ObjectiveArrowDir")
            [
                value("ARROWDOWN", 0),
                value("ARROWLEFT", 1),
                value("ARROWRIGHT", 2),
                value("ARROWUP", 3)
            ]
            .def(constructor<>())
            .def("SetObservationTarget", &GameActivity::SetObservationTarget)
            .def("SetDeathViewTarget", &GameActivity::SetDeathViewTarget)
            .def("SetLandingZone", &GameActivity::SetLandingZone)
            .def("GetLandingZone", &GameActivity::GetLandingZone)
            .def("SetActorSelectCursor", &GameActivity::SetActorSelectCursor)
            .def("GetBuyGUI", &GameActivity::GetBuyGUI)
            .def("GetEditorGUI", &GameActivity::GetEditorGUI)
            .property("WinnerTeam", &GameActivity::GetWinnerTeam, &GameActivity::SetWinnerTeam)
            .property("CPUTeam", &GameActivity::GetCPUTeam, &GameActivity::SetCPUTeam)
//            .def_readwrite("ActorCursor", &GameActivity::m_ActorCursor)
            .def_readwrite("CursorTimer", &GameActivity::m_CursorTimer)
            .def_readwrite("GameTimer", &GameActivity::m_GameTimer)
            .def_readwrite("GameOverTimer", &GameActivity::m_GameOverTimer)
            .def_readwrite("GameOverPeriod", &GameActivity::m_GameOverPeriod)
			.def("OtherTeam", &GameActivity::OtherTeam)
            .def("OneOrNoneTeamsLeft", &GameActivity::OneOrNoneTeamsLeft)
            .def("WhichTeamLeft", &GameActivity::WhichTeamLeft)
            .def("NoTeamLeft", &GameActivity::NoTeamLeft)
            // Backwards compat
            .def("OnlyOneTeamLeft", &GameActivity::OneOrNoneTeamsLeft)
            .def("GetBanner", &GameActivity::GetBanner)
            .def("SetLZArea", &GameActivity::SetLZArea)
            .def("GetLZArea", &GameActivity::GetLZArea)
            .def("SetBrainLZWidth", &GameActivity::SetBrainLZWidth)
            .def("GetBrainLZWidth", &GameActivity::GetBrainLZWidth)
			.def("GetActiveCPUTeamCount", &GameActivity::GetActiveCPUTeamCount)
			.def("GetActiveHumanTeamCount", &GameActivity::GetActiveHumanTeamCount)
			.def("AddObjectivePoint", &GameActivity::AddObjectivePoint)
            .def("YSortObjectivePoints", &GameActivity::YSortObjectivePoints)
            .def("ClearObjectivePoints", &GameActivity::ClearObjectivePoints)
            .def("AddOverridePurchase", &GameActivity::AddOverridePurchase)
            .def("SetOverridePurchaseList", (int (GameActivity::*)(const Loadout *, int))&GameActivity::SetOverridePurchaseList)
            .def("SetOverridePurchaseList", (int (GameActivity::*)(string, int))&GameActivity::SetOverridePurchaseList)
            .def("ClearOverridePurchase", &GameActivity::ClearOverridePurchase)
            .def("CreateDelivery", (bool (GameActivity::*)(int))&GameActivity::CreateDelivery)
            .def("CreateDelivery", (bool (GameActivity::*)(int, int))&GameActivity::CreateDelivery)
            .def("CreateDelivery", (bool (GameActivity::*)(int, int, Vector&))&GameActivity::CreateDelivery)
            .def("CreateDelivery", (bool (GameActivity::*)(int, int, Actor*))&GameActivity::CreateDelivery)
            .def("GetDeliveryCount", &GameActivity::GetDeliveryCount)
            .property("DeliveryDelay", &GameActivity::GetDeliveryDelay, &GameActivity::SetDeliveryDelay)
			.def("GetTeamTech", &GameActivity::GetTeamTech)
			.def("SetTeamTech", &GameActivity::SetTeamTech)
			.def("GetCrabToHumanSpawnRatio", &GameActivity::GetCrabToHumanSpawnRatio)
			.property("BuyMenuEnabled", &GameActivity::GetBuyMenuEnabled, &GameActivity::SetBuyMenuEnabled)
			.property("CraftsOrbitAtTheEdge", &GameActivity::GetCraftOrbitAtTheEdge, &GameActivity::SetCraftOrbitAtTheEdge)
            .def("TeamIsCPU", &GameActivity::TeamIsCPU)
            .def("GetStartingGold", &GameActivity::GetStartingGold)
            .def("GetFogOfWarEnabled", &GameActivity::GetFogOfWarEnabled)
            .def("UpdateEditing", &GameActivity::UpdateEditing)
            .def("DisableAIs", &GameActivity::DisableAIs)
            .def("InitAIs", &GameActivity::InitAIs)
            .def("AddPieMenuSlice", &GameActivity::AddPieMenuSlice)
            .def("AlterPieMenuSlice", &GameActivity::AlterPieMenuSlice)
            .def("RemovePieMenuSlice", &GameActivity::RemovePieMenuSlice)
			.def_readwrite("PieMenuSlices", &GameActivity::m_CurrentPieMenuSlices, return_stl_iterator),
		
		class_<PieMenuGUI::Slice>("Slice")
			.enum_("Direction") [
				value("NONE", 0),
				value("UP", 1),
				value("RIGHT", 2),
				value("DOWN", 3),
				value("LEFT", 4)
			]

			.enum_("Type") [
				value("PSI_NONE", 0),
				value("PSI_PICKUP", 1),
				value("PSI_DROP", 2),
				value("PSI_NEXTITEM", 3),
				value("PSI_PREVITEM", 4),
				value("PSI_RELOAD", 5),
				value("PSI_BUYMENU", 6),
				value("PSI_STATS", 7),
				value("PSI_MINIMAP", 8),
				value("PSI_FORMSQUAD", 9),
				value("PSI_CEASEFIRE", 10),
				value("PSI_SENTRY", 11),
				value("PSI_PATROL", 12),
				value("PSI_BRAINHUNT", 13),
				value("PSI_GOLDDIG", 14),
				value("PSI_GOTO", 15),
				value("PSI_RETURN", 16),
				value("PSI_STAY", 17),
				value("PSI_DELIVER", 18),
				value("PSI_SCUTTLE", 19),
				value("PSI_DONE", 20),
				value("PSI_LOAD", 21),
				value("PSI_SAVE", 22),
				value("PSI_NEW", 23),
				value("PSI_PICK", 24),
				value("PSI_MOVE", 25),
				value("PSI_REMOVE", 26),
				value("PSI_INFRONT", 27),
				value("PSI_BEHIND", 28),
				value("PSI_ZOOMIN", 29),
				value("PSI_ZOOMOUT", 30),
				value("PSI_TEAM1", 31),
				value("PSI_TEAM2", 32),
				value("PSI_TEAM3", 33),
				value("PSI_TEAM4", 34),
				value("PSI_SCRIPTED", 35),
				value("PSI_COUNT", 36)
			]

			.def(constructor<>())
			.property("FunctionName", &PieMenuGUI::Slice::GetFunctionName)
			.property("Description", &PieMenuGUI::Slice::GetDescription)
			.property("Type", &PieMenuGUI::Slice::GetType)
			.property("Direction", &PieMenuGUI::Slice::GetDirection),

        ABSTRACTLUABINDING(GlobalScript, Entity)
			.def("Deactivate", &GlobalScript::Deactivate),

        class_<ActivityMan>("ActivityManager")
            .property("DefaultActivityType", &ActivityMan::GetDefaultActivityType, &ActivityMan::SetDefaultActivityType)
            .property("DefaultActivityName", &ActivityMan::GetDefaultActivityName, &ActivityMan::SetDefaultActivityName)
            // Transfers ownership of the Activity to start into the ActivityMan, adopts ownership (_1 is the this ptr)
            .def("SetStartActivity", &ActivityMan::SetStartActivity, adopt(_2))
            .def("GetStartActivity", &ActivityMan::GetStartActivity)
            .def("GetActivity", &ActivityMan::GetActivity)
            // Transfers ownership of the Activity to start into the ActivityMan, adopts ownership (_1 is the this ptr)
            .def("StartActivity", (int (ActivityMan::*)(Activity *))&ActivityMan::StartActivity, adopt(_2))
            .def("StartActivity", (int (ActivityMan::*)(string, string))&ActivityMan::StartActivity)
            .def("RestartActivity", &ActivityMan::RestartActivity)
            .def("PauseActivity", &ActivityMan::PauseActivity)
            .def("EndActivity", &ActivityMan::EndActivity)
            .def("ActivityRunning", &ActivityMan::ActivityRunning)
            .def("ActivityPaused", &ActivityMan::ActivityPaused),

        class_<MetaPlayer>("MetaPlayer")
            .def(constructor<>())
            .property("NativeTechModule", &MetaPlayer::GetNativeTechModule)
            .property("ForeignCostMultiplier", &MetaPlayer::GetForeignCostMultiplier)
            .property("NativeCostMultiplier", &MetaPlayer::GetNativeCostMultiplier)
            .property("InGamePlayer", &MetaPlayer::GetInGamePlayer)
            .property("BrainPoolCount", &MetaPlayer::GetBrainPoolCount, &MetaPlayer::SetBrainPoolCount)
            .def("ChangeBrainPoolCount", &MetaPlayer::ChangeBrainPoolCount),

        class_<MetaMan>("MetaManager")
            .property("GameName", &MetaMan::GetGameName, &MetaMan::SetGameName)
            .property("PlayerTurn", &MetaMan::GetPlayerTurn)
            .property("PlayerCount", &MetaMan::GetPlayerCount)
            .def("GetTeamOfPlayer", &MetaMan::GetTeamOfPlayer)
            .def("GetPlayer", &MetaMan::GetPlayer)
            .def("GetMetaPlayerOfInGamePlayer", &MetaMan::GetMetaPlayerOfInGamePlayer)
            .def_readwrite("Players", &MetaMan::m_Players, return_stl_iterator),

        class_<AlarmEvent>("AlarmEvent")
            .def(constructor<>())
            .def(constructor<const Vector &, int, float>())
            .def_readwrite("ScenePos", &AlarmEvent::m_ScenePos)
            .def_readwrite("Team", &AlarmEvent::m_Team)
            .def_readwrite("Range", &AlarmEvent::m_Range),

        class_<MovableMan>("MovableManager")
            .def("GetMOFromID", &MovableMan::GetMOFromID)
			.def("FindObjectByUniqueID", &MovableMan::FindObjectByUniqueID)
			.def("GetMOIDCount", &MovableMan::GetMOIDCount)
			.def("GetTeamMOIDCount", &MovableMan::GetTeamMOIDCount)
            .def("PurgeAllMOs", &MovableMan::PurgeAllMOs)
            .def("GetNextActorInGroup", &MovableMan::GetNextActorInGroup)
            .def("GetPrevActorInGroup", &MovableMan::GetPrevActorInGroup)
            .def("GetNextTeamActor", &MovableMan::GetNextTeamActor)
            .def("GetPrevTeamActor", &MovableMan::GetPrevTeamActor)
            .def("GetClosestTeamActor", &MovableMan::GetClosestTeamActor)
            .def("GetClosestEnemyActor", &MovableMan::GetClosestEnemyActor)
            .def("GetFirstTeamActor", &MovableMan::GetFirstTeamActor)
            .def("GetClosestActor", &MovableMan::GetClosestActor)
            .def("GetClosestBrainActor", &MovableMan::GetClosestBrainActor)
            .def("GetFirstBrainActor", &MovableMan::GetFirstBrainActor)
            .def("GetClosestOtherBrainActor", &MovableMan::GetClosestOtherBrainActor)
            .def("GetFirstOtherBrainActor", &MovableMan::GetFirstOtherBrainActor)
            .def("GetUnassignedBrain", &MovableMan::GetUnassignedBrain)
            .def("GetParticleCount", &MovableMan::GetParticleCount)
            .def("GetAGResolution", &MovableMan::GetAGResolution)
            .def("GetSplashRatio", &MovableMan::GetSplashRatio)
            .property("MaxDroppedItems", &MovableMan::GetMaxDroppedItems, &MovableMan::SetMaxDroppedItems)
            .def("SortTeamRoster", &MovableMan::SortTeamRoster)
			.def("ChangeActorTeam", &MovableMan::ChangeActorTeam)
			.def("AddMO", &AddMO, adopt(_2))
            .def("AddActor", &AddActor, adopt(_2))
            .def("AddItem", &AddItem, adopt(_2))
            .def("AddParticle", &AddParticle, adopt(_2))
            .def("RemoveActor", &MovableMan::RemoveActor)
            .def("RemoveItem", &MovableMan::RemoveItem)
            .def("RemoveParticle", &MovableMan::RemoveParticle)
            .def("ValidMO", &MovableMan::ValidMO)
            .def("IsActor", &MovableMan::IsActor)
            .def("IsDevice", &MovableMan::IsDevice)
            .def("IsParticle", &MovableMan::IsParticle)
            .def("IsOfActor", &MovableMan::IsOfActor)
            .def("GetRootMOID", &MovableMan::GetRootMOID)
            .def("RemoveMO", &MovableMan::RemoveMO)
            .def("KillAllActors", &MovableMan::KillAllActors)
            .def("OpenAllDoors", &MovableMan::OpenAllDoors)
            .def("IsParticleSettlingEnabled", &MovableMan::IsParticleSettlingEnabled)
            .def("EnableParticleSettling", &MovableMan::EnableParticleSettling)
            .def("IsMOSubtractionEnabled", &MovableMan::IsMOSubtractionEnabled)
            .def_readwrite("Actors", &MovableMan::m_Actors, return_stl_iterator)
            .def_readwrite("Items", &MovableMan::m_Items, return_stl_iterator)
            .def_readwrite("Particles", &MovableMan::m_Particles, return_stl_iterator)
            .def_readwrite("AddedActors", &MovableMan::m_AddedActors, return_stl_iterator)
            .def_readwrite("AddedItems", &MovableMan::m_AddedItems, return_stl_iterator)
            .def_readwrite("AddedParticles", &MovableMan::m_AddedParticles, return_stl_iterator)
            .def_readwrite("AlarmEvents", &MovableMan::m_AlarmEvents, return_stl_iterator)
            .def_readwrite("AddedAlarmEvents", &MovableMan::m_AddedAlarmEvents, return_stl_iterator),

        class_<ConsoleMan>("ConsoleManager")
            .def("PrintString", &ConsoleMan::PrintString)
            .def("SaveInputLog", &ConsoleMan::SaveInputLog)
            .def("SaveAllText", &ConsoleMan::SaveAllText)
            .def("Clear", &ConsoleMan::ClearLog),

        class_<LuaMan>("LuaManager")
            .property("TempEntity", &LuaMan::GetTempEntity)
            .def_readonly("TempEntities", &LuaMan::m_TempEntityVector, return_stl_iterator)
            .def("FileOpen", &LuaMan::FileOpen)
            .def("FileClose", &LuaMan::FileClose)
            .def("FileReadLine", &LuaMan::FileReadLine)
            .def("FileWriteLine", &LuaMan::FileWriteLine)
            .def("FileEOF", &LuaMan::FileEOF),

        class_<SettingsMan>("SettingsManager")
            .property("PrintDebugInfo", &SettingsMan::PrintDebugInfo, &SettingsMan::SetPrintDebugInfo)
			.property("RecommendedMOIDCount", &SettingsMan::RecommendedMOIDCount),

        // NOT a member function, so adopting _1 instead of the _2 for the first param, since there's no "this" pointer!!
        def("DeleteEntity", &DeleteEntity, adopt(_1)),
        def("PosRand", &PosRand),
        def("NormalRand", &NormalRand),
        def("RangeRand", &RangeRand),
        def("SelectRand", &SelectRand),
        def("LERP", &LERP),
        def("EaseIn", &EaseIn),
        def("EaseOut", &EaseOut),
        def("EaseInOut", &EaseInOut),
        def("Clamp", &Limit),
		def("GetPPM", &GetPPM),
		def("GetMPP", &GetMPP),
		def("GetPPL", &GetPPL),
		def("GetLPP", &GetLPP),

		class_<enum_wrapper::input_device>("InputDevice")
			.enum_("InputDevice") [
				value("DEVICE_KEYB_ONLY", 0),
				value("DEVICE_MOUSE_KEYB", 1),
				value("DEVICE_GAMEPAD_1", 2),
				value("DEVICE_GAMEPAD_2", 3),
				value("DEVICE_GAMEPAD_3", 4),
				value("DEVICE_GAMEPAD_4", 5),
				value("DEVICE_COUNT", 6)
			],

		class_<enum_wrapper::input_elements>("InputElements")
			.enum_("InputElements") [
				value("INPUT_L_UP", 0),
				value("INPUT_L_DOWN", 1),
				value("INPUT_L_LEFT", 2),
				value("INPUT_L_RIGHT", 3),
				value("INPUT_R_UP", 4),
				value("INPUT_R_DOWN", 5),
				value("INPUT_R_LEFT", 6),
				value("INPUT_R_RIGHT", 7),
				value("INPUT_FIRE", 8),
				value("INPUT_AIM", 9),
				value("INPUT_AIM_UP", 10),
				value("INPUT_AIM_DOWN", 11),
				value("INPUT_AIM_LEFT", 12),
				value("INPUT_AIM_RIGHT", 13),
				value("INPUT_PIEMENU", 14),
				value("INPUT_JUMP", 15),
				value("INPUT_CROUCH", 16),
				value("INPUT_NEXT", 17),
				value("INPUT_PREV", 18),
				value("INPUT_START", 19),
				value("INPUT_BACK", 20),
				value("INPUT_COUNT", 21)
			],

		class_<enum_wrapper::mouse_buttons>("MouseButtons")
			.enum_("MouseButtons") [
				value("MOUSE_NONE", -1),
				value("MOUSE_LEFT", 0),
				value("MOUSE_RIGHT", 1),
				value("MOUSE_MIDDLE", 2),
				value("MAX_MOUSE_BUTTONS", 3)
			],

		class_<enum_wrapper::joy_buttons>("JoyButtons")
			.enum_("JoyButtons") [
				value("JOY_NONE", -1),
				value("JOY_1", 0),
				value("JOY_2", 1),
				value("JOY_3", 2),
				value("JOY_4", 3),
				value("JOY_5", 4),
				value("JOY_6", 5),
				value("JOY_7", 6),
				value("JOY_8", 7),
				value("JOY_9", 8),
				value("JOY_10", 9),
				value("JOY_11", 10),
				value("JOY_12", 11),
				value("MAX_JOY_BUTTONS", 12)
			],

		class_<enum_wrapper::joy_directions>("JoyDirections")
			.enum_("JoyDirections") [
				value("JOYDIR_ONE", 0),
				value("JOYDIR_TWO", 1)
			]
    ];

    // Assign the manager instances to globals in the lua master state
    globals(m_pMasterState)["TimerMan"] = &g_TimerMan;
    globals(m_pMasterState)["FrameMan"] = &g_FrameMan;
	globals(m_pMasterState)["PostProcessMan"] = &g_PostProcessMan;
	globals(m_pMasterState)["PrimitiveMan"] = &g_PrimitiveMan;
    globals(m_pMasterState)["PresetMan"] = &g_PresetMan;
    globals(m_pMasterState)["AudioMan"] = &g_AudioMan;
    globals(m_pMasterState)["UInputMan"] = &g_UInputMan;
    globals(m_pMasterState)["SceneMan"] = &g_SceneMan;
    globals(m_pMasterState)["ActivityMan"] = &g_ActivityMan;
    globals(m_pMasterState)["MetaMan"] = &g_MetaMan;
    globals(m_pMasterState)["MovableMan"] = &g_MovableMan;
    globals(m_pMasterState)["ConsoleMan"] = &g_ConsoleMan;
    globals(m_pMasterState)["LuaMan"] = &g_LuaMan;
    globals(m_pMasterState)["SettingsMan"] = &g_SettingsMan;

    luaL_dostring(m_pMasterState,
        // Override print() in the lua state to output to the console
        "print = function(toPrint) ConsoleMan:PrintString(\"PRINT: \" .. tostring(toPrint)); end;\n"
        // Add cls() as a shorcut to ConsoleMan:Clear()
        "cls = function() ConsoleMan:Clear(); end;"
        // Add package path to the defaults
        "package.path = package.path .. \";Base.rte/?.lua\";\n"
    );

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::ClearUserModuleCache()
{
	luaL_dostring(m_pMasterState, "for m, n in pairs(package.loaded) do if type(n) == \"boolean\" then package.loaded[m] = nil end end");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::Destroy()
{
    lua_close(m_pMasterState);

	//Close all opened files
	for (int i = 0; i < MAX_OPEN_FILES; ++i)
		FileClose(i);

    Clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int LuaMan::SavePointerAsGlobal(void *pToSave, string globalName)
{
    // Push the pointer onto the Lua stack
    lua_pushlightuserdata(m_pMasterState, pToSave);
    // Pop and assign that pointer to a global var in the Lua state
    lua_setglobal(m_pMasterState, globalName.c_str());

    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LuaMan::GlobalIsDefined(string globalName)
{
    // Get the var you want onto the stack so we can check it
    lua_getglobal(m_pMasterState, globalName.c_str());
    // Now report if it is nil/null or not
    bool isDefined = !lua_isnil(m_pMasterState, -1);
    // Pop the var so this operation is balanced and leaves the stack as it was
    lua_pop(m_pMasterState, 1);

    return isDefined;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LuaMan::TableEntryIsDefined(string tableName, string indexName)
{
    // Push the table onto the stack, checking if it even exists
    lua_getglobal(m_pMasterState, tableName.c_str());
    if (!lua_istable(m_pMasterState, -1))
    {
//        error(m_pMasterState, tableName + " is not a table when checking for the " + indexName + " within it.");
        // Clean up and report that there was nothing properly defined here
        lua_pop(m_pMasterState, 1);
        return false;
    }

    // Push the value at the requested index onto the stack so we can check if it's anything
    lua_getfield(m_pMasterState, -1, indexName.c_str());
    // Now report if it is nil/null or not
    bool isDefined = !lua_isnil(m_pMasterState, -1);
    // Pop both the var and the table so this operation is balanced and leaves the stack as it was
    lua_pop(m_pMasterState, 2);

    return isDefined;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LuaMan::ExpressionIsTrue(string expression, bool consoleErrors)
{
    if (expression.empty())
        return false;

    bool result = false;

    try
    {
        // Push the script string onto the stack so we can execute it, and then actually try to run it
        // Assign the result to a dedicated temp global variable
        if (luaL_dostring(m_pMasterState, (string("ExpressionResult = ") + expression + string(";")).c_str()))
        {
            // Retrieve and pop the error message off the stack
            m_LastError = string("When evaluating Lua expression: ") + lua_tostring(m_pMasterState, -1);
            lua_pop(m_pMasterState, 1);
            if (consoleErrors)
            {
                g_ConsoleMan.PrintString("ERROR: " + m_LastError);
                ClearErrors();
            }
            return false;
        }
    }
    catch(const std::exception &e)
    {
        m_LastError = string("When evaluating Lua expression: ") + e.what();
        if (consoleErrors)
        {
            g_ConsoleMan.PrintString("ERROR: " + m_LastError);
            ClearErrors();
        }
        return false;
    }

    // Get the result var onto the stack so we can check it
    lua_getglobal(m_pMasterState, "ExpressionResult");
    // Now report if it is nil/null or not
    result = lua_toboolean(m_pMasterState, -1);
    // Pop the result var so this operation is balanced and leaves the stack as it was
    lua_pop(m_pMasterState, 1);

    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int LuaMan::RunScriptedFunction(const std::string &functionName, const std::string &selfObjectName, std::vector<std::string> variablesToSafetyCheck, std::vector<Entity *> functionEntityArguments, std::vector<std::string> functionLiteralArguments) {
    std::string scriptString = "";
    if (!variablesToSafetyCheck.empty()) {
        scriptString += "if ";
        for (const std::string &variableToSafetyCheck : variablesToSafetyCheck) {
            if (&variableToSafetyCheck != &variablesToSafetyCheck[0]) {
                scriptString += " and ";
            }
            scriptString += variableToSafetyCheck;
        }
        scriptString += " then ";
    }
    if (!functionEntityArguments.empty()) {
        scriptString += "local entityArguments = LuaMan.TempEntities; ";
    }
    scriptString += functionName + "(" + selfObjectName;
    if (!functionEntityArguments.empty()) {
        g_LuaMan.SetTempEntityVector(functionEntityArguments);
        for (const Entity *functionEntityArgument : functionEntityArguments) {
            scriptString += ", (To" + functionEntityArgument->GetClassName() + " and To" + functionEntityArgument->GetClassName() + "(entityArguments()) or entityArguments())";
        }
    }
    if (!functionLiteralArguments.empty()) {
        for (const std::string functionLiteralArgument : functionLiteralArguments) {
            scriptString += ", " + functionLiteralArgument;
        }
    }
    scriptString += ");";

    if (!variablesToSafetyCheck.empty()) { scriptString += " end;"; }
    
    return RunScriptString(scriptString);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int LuaMan::RunScriptString(const std::string &scriptString, bool consoleErrors) {
    if (scriptString.empty()) {
        return -1;
    }
    int error = 0;

    lua_pushcfunction(m_pMasterState, &AddFileAndLineToError);
    try {
        // Load the script string onto the stack and then execute it with pcall. Pcall will call the file and line error handler if there's an error by pointing 2 up the stack to it.
        if (luaL_loadstring(m_pMasterState, scriptString.c_str()) || lua_pcall(m_pMasterState, 0, LUA_MULTRET, -2)) {
            // Retrieve the error message then pop it off the stack to clean it up
			m_LastError = lua_tostring(m_pMasterState, -1);
            lua_pop(m_pMasterState, 1);
            if (consoleErrors) {
                g_ConsoleMan.PrintString("ERROR: " + m_LastError);
                ClearErrors();
            }
            error = -1;
        }
    } catch(const std::exception &e) {
        m_LastError = e.what();
        if (consoleErrors) {
            g_ConsoleMan.PrintString("ERROR: " + m_LastError);
            ClearErrors();
        }
        error = -1;
    }

    // Pop the file and line error handler off the stack to clean it up
    lua_pop(m_pMasterState, 1);
        
    return error;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int LuaMan::RunScriptFile(const std::string &filePath, bool consoleErrors) {
    if (filePath.empty()) {
        m_LastError = "Can't run a script file with an empty filepath!";
        return -1;
    }
    int error = 0;

    lua_pushcfunction(m_pMasterState, &AddFileAndLineToError);
    try {
        // Load the script file's contents onto the stack and then execute it with pcall. Pcall will call the file and line error handler if there's an error by pointing 2 up the stack to it.
        if (luaL_loadfile(m_pMasterState, filePath.c_str()) || lua_pcall(m_pMasterState, 0, LUA_MULTRET, -2)) {
            // Retrieve the error message then pop it off the stack
            m_LastError = lua_tostring(m_pMasterState, -1);
            lua_pop(m_pMasterState, 1);
            if (consoleErrors) {
                g_ConsoleMan.PrintString("ERROR: " + m_LastError);
                ClearErrors();
            }
            error = -1;
        }
    } catch(const std::exception &e) {
        m_LastError = e.what();
        if (consoleErrors) {
            g_ConsoleMan.PrintString("ERROR: " + m_LastError);
            ClearErrors();
        }
        error = -1;
    }

    // Pop the file and line error handler off the stack to clean it up
    lua_pop(m_pMasterState, 1);

    return error;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

string LuaMan::GetNewPresetID()
{
    // Generate the new ID
    char newID[64];
    sprintf_s(newID, sizeof(newID), "Pre%05i", m_NextPresetID);
    // Increment the ID so it will be diff for the next one (improve this primitive approach??)
    m_NextPresetID++;

    return string(newID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

string LuaMan::GetNewObjectID()
{
    // Generate the new ID
    char newID[64];
    sprintf_s(newID, sizeof(newID), "Obj%05i", m_NextObjectID);
    // Increment the ID so it will be diff for the next one (improve this primitive approach??)
    m_NextObjectID++;

    return string(newID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::Update()
{
	lua_gc(m_pMasterState, LUA_GCSTEP, 1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int LuaMan::FileOpen(std::string filename, std::string mode)
{
	int fl = -1;

	// Find some suitable file
	for (int i = 0; i < MAX_OPEN_FILES; ++i)
		if (m_Files[i] == 0)
		{
			fl = i;
			break;
		}

	if (fl == -1)
	{
		g_ConsoleMan.PrintString("Error: Can't open file, no more slots.");
		return -1;
	}

	//Check for path back-traversing and .rte extension. Everything is allowed to read or write only inside rte's
    string dotString = "..";
	string rteString = ".rte";

	string fullPath = g_System.GetWorkingDirectory()+ "/" + filename;

	// Do not open paths with '..'
	if (fullPath.find(dotString) != string::npos)
		return -1;

	// Allow to edit files only inside .rte folders
	if (fullPath.find(rteString) == string::npos)
		return -1;

	// Open file and save handle
	FILE * f = fopen(fullPath.c_str(), mode.c_str());
	if (f)
	{
		m_Files[fl] = f;
		return fl;
	}

#ifdef _DEBUG
	g_ConsoleMan.PrintString("Error: Can't open file. " + fullPath);
#endif
	return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::FileClose(int file)
{
	if (file > -1 && file < MAX_OPEN_FILES && m_Files[file])
	{
		fclose(m_Files[file]);
		m_Files[file] = 0;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::FileCloseAll()
{
	for (int file = 0 ; file < MAX_OPEN_FILES ; ++file)
		FileClose(file);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string LuaMan::FileReadLine(int file)
{
	if (file > -1 && file < MAX_OPEN_FILES && m_Files[file])
	{
		char buf[4096];
		fgets(buf, 4095, m_Files[file]);
		return (std::string(buf));
	}
	g_ConsoleMan.PrintString("Error: Tried to read a closed file, or read past EOF.");
	return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LuaMan::FileWriteLine(int file, std::string line)
{
	if (file > -1 && file < MAX_OPEN_FILES && m_Files[file])
		fputs(line.c_str(), m_Files[file]);
	else
		g_ConsoleMan.PrintString("Error: Tried to write to a closed file.");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LuaMan::FileEOF(int file)
{
	if (file > -1 && file < MAX_OPEN_FILES && m_Files[file])
		if (!feof(m_Files[file]))
			return false;
	return true;
}
}