#include <set>
#include <queue>
#include <limits>
#include <stdexcept>
#include <chrono>
#include <napi.h>
#include "Offsets.h"
#include "unit/GameObject.h"
#include "Farsight.h"
#include "windows.h"
#include "psapi.h"
#include "Utils.h"

std::set<std::string> Farsight::championNames{};

Farsight::Farsight()
{
    // Some trash object not worth reading, most likely outdated
    blacklistedObjectNames.insert("testcube");
    blacklistedObjectNames.insert("testcuberender");
    blacklistedObjectNames.insert("testcuberender10vision");
    blacklistedObjectNames.insert("s5test_wardcorpse");
    blacklistedObjectNames.insert("sru_camprespawnmarker");
    blacklistedObjectNames.insert("sru_plantrespawnmarker");
    blacklistedObjectNames.insert("preseason_turret_shield");
}

bool Farsight::IsLeagueRunning()
{
    // Check if the process is running
    hWindow = FindWindowA(NULL, "League of Legends (TM) Client");

    DWORD h;
    GetWindowThreadProcessId(hWindow, &h);
    return pid == h;
}

bool Farsight::IsHooked()
{
    return Process::IsProcessRunning(pid);
}


void Farsight::UnhookFromProcess()
{
    CloseHandle(hProcess);
    hProcess = NULL;
    pid = 0;
    hWindow = NULL;
    baseAddress = 0;
    size = 0;
    isSixtyFourBit = FALSE;
}

void Farsight::HookToProcess()
{
    // Get the process id
    hWindow = FindWindowA("RiotWindowClass", NULL);

    if (hWindow == NULL)
        throw WinApiException("Could not find League of Legends window");

    GetWindowThreadProcessId(hWindow, &pid);

    if (pid == NULL || !Process::IsProcessRunning(pid))
        throw WinApiException("Could not find League of Legends process");

    // Open the process
    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);

    if (hProcess == NULL)
        throw WinApiException("Could not open League of Legends process");

    if (0 == IsWow64Process(hProcess, &isSixtyFourBit))
        throw WinApiException("Could not determine bitness of League of Legends process");

    HMODULE hMods[1024];
    DWORD cbNeeded;
    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
    {
        baseAddress = (DWORD_PTR)hMods[0];
    }
    else
    {
        throw WinApiException("Couldn't retrieve league base address");
    }

    blacklistedObjects.clear();
}

void Farsight::ClearMissingObjects(Snapshot &snapshot)
{
    auto current = snapshot.objectMap.begin();

    while (current != snapshot.objectMap.end())
    {
        if (snapshot.updatedObjects.find(current->first) == snapshot.updatedObjects.end())
        {
            current = snapshot.objectMap.erase(current);
        }
        else
            ++current;
    }
}

void Farsight::ReadObjects(Snapshot &snapshot)
{

    static const int maxObjects = 1024;
    static int pointerArray[maxObjects];

    std::chrono::high_resolution_clock::time_point readTimeBegin;
    std::chrono::duration<float, std::milli> readDuration;
    readTimeBegin = std::chrono::high_resolution_clock::now();

    snapshot.champions.clear();
    snapshot.jungle.clear();
    snapshot.inhibitors.clear();
    snapshot.turrets.clear();
    snapshot.other.clear();
    snapshot.nextDragonType.clear();

    int objectManager = Memory::ReadDWORD(hProcess, baseAddress + Offsets::ObjectManager);

    static char buff[0x500];
    Memory::Read(hProcess, objectManager, buff, 0x100);

    int rootNode;
    memcpy(&rootNode, buff + Offsets::ObjectMapRoot, sizeof(int));

    std::queue<int> nodesToVisit;
    std::set<int> visitedNodes;
    nodesToVisit.push(rootNode);

    // Read object pointers from tree
    int nrObj = 0;
    int reads = 0;
    int childNode1, childNode2, childNode3, node;
    while (reads < maxObjects && nodesToVisit.size() > 0)
    {
        node = nodesToVisit.front();
        nodesToVisit.pop();
        if (visitedNodes.find(node) != visitedNodes.end())
            continue;

        reads++;
        visitedNodes.insert(node);
        Memory::Read(hProcess, node, buff, 0x30);

        memcpy(&childNode1, buff, sizeof(int));
        memcpy(&childNode2, buff + 4, sizeof(int));
        memcpy(&childNode3, buff + 8, sizeof(int));

        nodesToVisit.push(childNode1);
        nodesToVisit.push(childNode2);
        nodesToVisit.push(childNode3);

        unsigned int netId = 0;
        memcpy(&netId, buff + Offsets::ObjectMapNodeNetId, sizeof(int));

        // Check all network ids instead of only the largest because we want troys as well
        /*
        if (netId - (unsigned int)0x40000000 > 0x100000)
            continue;
        */

        int addr;
        memcpy(&addr, buff + Offsets::ObjectMapNodeObject, sizeof(int));
        if (addr == 0)
            continue;

        pointerArray[nrObj] = addr;
        nrObj++;
    }

    // Read objects from the pointers we just read
    for (int i = 0; i < nrObj; ++i)
    {
        int netId;
        Memory::Read(hProcess, pointerArray[i] + Offsets::ObjNetworkID, &netId, sizeof(int));
        if (blacklistedObjects.find(netId) != blacklistedObjects.end())
            continue;

        std::shared_ptr<GameObject> obj;
        auto it = snapshot.objectMap.find(netId);
        if (it == snapshot.objectMap.end())
        {
            obj = std::shared_ptr<GameObject>(new GameObject());
            obj->LoadFromMemory(pointerArray[i], hProcess, true);
            snapshot.objectMap[obj->networkId] = obj;
        }
        else
        {
            obj = it->second;
            obj->LoadFromMemory(pointerArray[i], hProcess, true);

            if (netId != obj->networkId)
            {
                snapshot.objectMap[obj->networkId] = obj;
            }
        }

        if (obj->networkId == 0)
        {
            continue;
        }

        snapshot.indexToNetId[obj->objectIndex] = obj->networkId;
        snapshot.updatedObjects.insert(obj->networkId);

        /* Do not filter out names to include troy objects
        if(obj->name.size() < 2 || blacklistedObjectNames.find(obj->name) != blacklistedObjectNames.end()) {
            blacklistedObjects.insert(obj->networkId);
            continue;
        }

        */

        if (championNames.find(obj->name) != championNames.end())
        {
            obj->LoadChampionData();
            snapshot.champions.push_back(obj);
            continue;
        }

        const char *inhibitorText = "Barracks_T";
        const char *turretText = "Turret_T";

        if (Character::VectorStartsWith(obj->displayName, inhibitorText))
        {
            snapshot.inhibitors.push_back(obj);
            continue;
        }

        if (Character::VectorStartsWith(obj->displayName, turretText))
        {
            snapshot.turrets.push_back(obj);
            continue;
        }

        if(Character::VectorStartsWith(obj->displayName, "Dragon_Indicator_"))
        {
            std::vector<char> dragonType(obj->displayName);
            dragonType.erase(dragonType.end() - 5, dragonType.end());
            dragonType.erase(dragonType.begin(), dragonType.begin() + 17);
            snapshot.nextDragonType = std::string(dragonType.begin(), dragonType.end());

            continue;
        }

        if (obj->name.find("sru_dragon", 0) != std::string::npos)
        {
            snapshot.jungle.push_back(obj);
            continue;
        }

        if (obj->name.find("sru_baron", 0) != std::string::npos)
        {
            snapshot.jungle.push_back(obj);
            continue;
        }

        if (obj->name.find("sru_riftherald", 0) != std::string::npos)
        {
            snapshot.jungle.push_back(obj);
            continue;
        }

        snapshot.other.push_back(obj);
    }

    readDuration = std::chrono::high_resolution_clock::now() - readTimeBegin;
    snapshot.benchmark->readObjectsMs = readDuration.count();
}

void Farsight::ReadChampions(ChampionSnapshot &snapshot)
{
}

void Farsight::CreateSnapshot(Snapshot &snapshot)
{
    Memory::Read(hProcess, baseAddress + Offsets::GameTime, &snapshot.gameTime, sizeof(float));

    if (snapshot.gameTime <= 5.0f)
        return;

    ReadObjects(snapshot);
    ClearMissingObjects(snapshot);
};

void Farsight::CreateChampionSnapshot(ChampionSnapshot &championSnapshot)
{
    Memory::Read(hProcess, baseAddress + Offsets::GameTime, &championSnapshot.gameTime, sizeof(float));
}