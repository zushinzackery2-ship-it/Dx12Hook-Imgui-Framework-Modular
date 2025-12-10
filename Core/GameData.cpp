#include "GameData.h"
#include "GameMemory.h"
#include "Config.h"
#include <algorithm>

using namespace SDK;
using namespace GameMemory;

namespace GameData {
    
    DataCollector::DataCollector() 
        : m_Running(false), m_Thread(nullptr), m_ActorCount(0) {
        InitializeCriticalSection(&m_CriticalSection);
        m_GameBase = GetModuleHandleA(nullptr);
        m_Actors.reserve(1000); // 预分配空间
    }
    
    DataCollector::~DataCollector() {
        StopCollection();
        DeleteCriticalSection(&m_CriticalSection);
    }
    
    void DataCollector::StartCollection() {
        if (m_Running) return;
        m_Running = true;
        m_Thread = CreateThread(nullptr, 0, CollectionThread, this, 0, nullptr);
    }
    
    void DataCollector::StopCollection() {
        m_Running = false;
        if (m_Thread) {
            WaitForSingleObject(m_Thread, 1000);
            CloseHandle(m_Thread);
            m_Thread = nullptr;
        }
    }
    
    DWORD WINAPI DataCollector::CollectionThread(LPVOID param) {
        auto* collector = static_cast<DataCollector*>(param);
        while (collector->m_Running) {
            collector->CollectData();
            Sleep(16); // ~60 FPS
        }
        return 0;
    }
    
    void DataCollector::CollectData() {
        // 读取游戏世界
        auto GWorld = (UWorld*)ReadPtr((DWORD64)m_GameBase + Config::Offsets::GWorld);
        if (!GWorld) return;
        
        auto GameInstance = GWorld->OwningGameInstance;
        if (!GameInstance) return;
        
        auto LocalPlayers = GameInstance->LocalPlayers;
        if (!LocalPlayers || LocalPlayers.Num() <= 0) return;
        
        auto PlayerController = LocalPlayers[0]->PlayerController;
        if (!PlayerController) return;
        
        auto PlayerCameraManager = PlayerController->PlayerCameraManager;
        if (!PlayerCameraManager) return;
        
        FVector CameraLocation = PlayerCameraManager->CameraCachePrivate.POV.Location;
        if (CameraLocation.IsZero()) return;
        
        auto PlayerPawn = PlayerController->Pawn;
        if (!PlayerPawn) return;
        
        // 获取关卡中的所有Actor
        auto Level = GWorld->PersistentLevel;
        if (!Level) return;
        
        auto Actors = Level->Actors;
        if (!Actors) return;
        
        uint32_t ActorCount = Actors.Num();
        if (ActorCount <= 0) return;
        
        // 进入临界区
        EnterCriticalSection(&m_CriticalSection);
        
        m_Actors.clear();
        m_ActorCount = 0;
        
        // 调试计数器（仅输出前10个匹配的物品）
        static int debugCount = 0;
        
        for (uint32_t i = 0; i < ActorCount; i++) {
            auto Actor = Actors[i];
            if (!Actor || !Actor->Class) continue;
            
            // 直接按类名前缀过滤物品
            std::string ClassName = Actor->Class->GetName();
            if (ClassName.rfind("BP_DroppedItem_", 0) != 0) continue;  // 只保留 BP_DroppedItem_* 类
            
            // 调试输出（仅前10个）
            if (debugCount < 10) {
                OutputDebugStringA(("[ESP] 找到物品类: " + ClassName + "\n").c_str());
                debugCount++;
            }
            
            ActorInfo info;
            info.Name = Actor->GetName();
            info.ClassName = ClassName;
            info.Index = i;
            info.Location = Actor->K2_GetActorLocation();
            if (info.Location.IsZero()) continue;
            
            // 提取显示名称: BP_DroppedItem_XXX_C -> XXX
            size_t prefixPos = ClassName.find("BP_DroppedItem_");
            if (prefixPos != std::string::npos) {
                size_t startPos = prefixPos + 15;  // "BP_DroppedItem_" 长度
                size_t endPos = ClassName.find("_C", startPos);
                if (endPos != std::string::npos) {
                    info.DisplayName = ClassName.substr(startPos, endPos - startPos);
                } else {
                    info.DisplayName = ClassName.substr(startPos);
                }
            } else {
                info.DisplayName = ClassName;
            }
            
            // 计算距离
            auto PlayerLocation = PlayerPawn->K2_GetActorLocation();
            info.Distance = PlayerLocation.GetDistanceTo(info.Location);
            
            info.IsVisible = PlayerController->LineOfSightTo(Actor, CameraLocation, true);
            PlayerController->ProjectWorldLocationToScreen(info.Location, &info.ScreenLocation, true);
            
            m_Actors.push_back(info);
            m_ActorCount++;
        }
        
        // 速度修改功能 (APawn + 0x98 = CustomTimeDilation)
        if (PlayerPawn) {
            float targetDilation = Config::Features::SpeedBoost_Enabled
                ? Config::Features::SpeedMultiplier
                : 1.0f;
            WriteFloat((DWORD64)PlayerPawn + Config::Offsets::PlayerSpeedOffset, targetDilation);
        }
        
        // 退出临界区
        LeaveCriticalSection(&m_CriticalSection);
    }
    
    std::vector<ActorInfo> DataCollector::GetActors() {
        EnterCriticalSection(&m_CriticalSection);
        auto actors = m_Actors;
        LeaveCriticalSection(&m_CriticalSection);
        return actors;
    }
    
    int DataCollector::GetActorCount() {
        EnterCriticalSection(&m_CriticalSection);
        int count = m_ActorCount;
        LeaveCriticalSection(&m_CriticalSection);
        return count;
    }
}
