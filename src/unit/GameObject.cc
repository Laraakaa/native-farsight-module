#include "GameObject.h"
#include "../Utils.h"
#include "../Farsight.h"
#include "../Offsets.h"
#include <napi.h>
#include <vector>

BYTE GameObject::buff[GameObject::sizeBuff] = {};
BYTE GameObject::buffDeep[GameObject::sizeBuffDeep] = {};

bool GameObject::IsEqualTo(const GameObject &other) const
{
    return this->objectIndex == other.objectIndex;
}

bool GameObject::IsNotEqualTo(const GameObject &other) const
{
    return this->objectIndex != other.objectIndex;
}

void GameObject::LoadFromMemory(DWORD base, HANDLE hProcess, bool deepLoad)
{
    baseAddress = base;
    Memory::Read(hProcess, base, buff, sizeBuff);

    memcpy(&networkId, &buff[Offsets::ObjNetworkID], sizeof(DWORD));
    memcpy(&objectIndex, &buff[Offsets::ObjIndex], sizeof(short));
    memcpy(&team, &buff[Offsets::ObjTeam], sizeof(short));
    memcpy(&position, &buff[Offsets::ObjPosition], sizeof(float) * 3);
    memcpy(&health, &buff[Offsets::ObjHealth], sizeof(float));
    memcpy(&maxHealth, &buff[Offsets::ObjMaxHealth], sizeof(float));
    memcpy(&mana, &buff[Offsets::ObjMana], sizeof(float));
    memcpy(&maxMana, &buff[Offsets::ObjMaxMana], sizeof(float));

    isAlive = health > 0;

    if (deepLoad)
    {
        char nameBuff[50];
        Memory::Read(hProcess, Memory::ReadDWORDFromBuffer(buff, Offsets::ObjName), nameBuff, 50);

        if (Character::ContainsOnlyASCII(nameBuff, 50))
        {
            name = Character::ToLower(std::string(nameBuff));
        }
        else
        {
            name = "";
        }

        int displayNameLength = Memory::ReadDWORDFromBuffer(buff, Offsets::ObjDisplayNameLength);

        if (displayNameLength < 16)
        {
            displayName.resize(displayNameLength);
            memcpy(displayName.data(), &buff[Offsets::ObjDisplayName], displayNameLength);
        }
        else
        {
            char displayNameBuff[50];
            Memory::Read(hProcess, Memory::ReadDWORDFromBuffer(buff, Offsets::ObjDisplayName), displayNameBuff, 50);
            displayName.resize(displayNameLength);
            memcpy(displayName.data(), &displayNameBuff[0], displayNameLength);
        }
    }
}

void GameObject::LoadChampionData()
{
    isChampion = true;
    memcpy(&experience, &buff[Offsets::ObjExperience], sizeof(float));
    memcpy(&level, &buff[Offsets::ObjLvl], sizeof(int));
    memcpy(&currentGold, &buff[Offsets::ObjCurrentGold], sizeof(float));
    memcpy(&totalGold, &buff[Offsets::ObjTotalGold], sizeof(float));
}

bool GameObject::IsChampion()
{
    return isChampion;
}

Napi::Object GameObject::ToNapiObject(Napi::Env env)
{
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("name", name);
    Napi::Buffer<char> displayNameBuffer = Napi::Buffer<char>::Copy(env, displayName.data(), displayName.size());
    obj.Set("displayName", displayNameBuffer);
    obj.Set("networkId", networkId);
    obj.Set("objectIndex", objectIndex);
    Napi::Float32Array jsPosition = Napi::Float32Array::New(env, 3);
    jsPosition[0] = position[0];
    jsPosition[1] = position[1];
    jsPosition[2] = position[2];
    obj.Set("position", jsPosition);
    obj.Set("team", team);
    obj.Set("isAlive", isAlive);
    obj.Set("health", health);
    obj.Set("maxHealth", maxHealth);
    obj.Set("mana", mana);
    obj.Set("maxMana", maxMana);
    obj.Set("address", Napi::Number::New(env, baseAddress));

    if (!isChampion)
        return obj;

    obj.Set("currentGold", currentGold);
    obj.Set("totalGold", totalGold);
    obj.Set("experience", experience);
    obj.Set("level", level);

    return obj;
}