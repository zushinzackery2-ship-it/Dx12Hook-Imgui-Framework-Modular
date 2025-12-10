#include "Renderer.h"
#include "../Core/Config.h"

namespace UI {
    
    void Renderer::Initialize() {
        SetupStyle();
    }
    
    void Renderer::SetupStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;
        
        // 锐角边框（圆角设为0）+ 间距
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.ChildRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.WindowPadding = ImVec2(18, 16);
        style.FramePadding = ImVec2(10, 6);
        style.ItemSpacing = ImVec2(12, 10);
        
        // 高透明度主题（alpha降低）
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.55f, 0.60f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.08f, 0.09f, 0.12f, 0.95f);
        colors[ImGuiCol_Border]                 = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.14f, 0.15f, 0.20f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.20f, 0.80f, 0.60f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.50f, 0.90f, 0.80f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.26f, 0.60f, 0.98f, 0.90f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.16f, 0.48f, 0.85f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.23f, 0.27f, 0.36f, 1.00f);
        
        // 字体缩放
        ImGui::GetIO().FontGlobalScale = Config::UI::FontScale;
        ImGui::GetIO().FontAllowUserScaling = false;
    }
    
    void Renderer::RenderMenu(GameData::DataCollector* dataCollector) {
        ImGui::SetNextWindowSize(ImVec2(Config::UI::MenuWidth, Config::UI::MenuHeight), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("UI-EscapeTheBackrooms", nullptr, 0)) {
            // 标题
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 1.00f, 1.00f));
            ImGui::Text(">> 功能列表");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // 功能开关
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
            ImGui::Checkbox("  物品透视", &Config::Features::ESP_Enabled);
            ImGui::SameLine();
            ImGui::TextDisabled("[F1]");
            ImGui::Spacing();
            
            ImGui::Checkbox("  加速功能", &Config::Features::SpeedBoost_Enabled);
            ImGui::SameLine();
            ImGui::TextDisabled("[F2]");
            ImGui::PopStyleVar();
            
            // 速度倍率滑块（仅在加速功能启用时显示）
            if (Config::Features::SpeedBoost_Enabled) {
                ImGui::Spacing();
                ImGui::PushItemWidth(200);
                ImGui::SliderFloat("速度倍率", &Config::Features::SpeedMultiplier, 0.5f, 5.0f, "%.1fx");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::TextDisabled("(正常=1.0x)");
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // 统计信息
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.80f, 0.00f, 1.00f));
            ImGui::Text(">> 状态信息");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::Text("检测到物品: %d", dataCollector->GetActorCount());
            ImGui::Spacing();
            
            // 提示信息
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.70f, 0.70f, 1.00f));
            ImGui::TextWrapped("按 INSERT 键切换菜单显示");
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }
    
    void Renderer::RenderESP(GameData::DataCollector* dataCollector) {
        if (!Config::Features::ESP_Enabled) return;
        
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ImVec2 antennaStart(screenSize.x / 2.0f, 20.0f);
        
        // 显示物品数量
        char text[64];
        sprintf_s(text, "物品数量: %d", dataCollector->GetActorCount());
        drawList->AddText(ImVec2(screenSize.x / 2.0f - 80.0f, 20.0f), 
                         IM_COL32(255, 255, 0, 255), text);
        
        // 绘制所有物品
        auto actors = dataCollector->GetActors();
        for (const auto& actor : actors) {
            // 蓝色：可见=亮蓝色，遮挡=深蓝色
            ImU32 color = actor.IsVisible ? IM_COL32(0, 200, 255, 255) : IM_COL32(0, 100, 200, 255);
            ImVec2 screenPos(actor.ScreenLocation.X, actor.ScreenLocation.Y);
            
            // 根据距离计算圆框大小 (近大远小)
            float distanceMeters = actor.Distance / 100.0f;  // 厘米转米
            float radius = 30.0f / (1.0f + distanceMeters / 10.0f);  // 10米处半径15像素
            if (radius < 5.0f) radius = 5.0f;    // 最小5像素
            if (radius > 30.0f) radius = 30.0f;  // 最大30像素
            
            // 绘制圆框（空心）
            drawList->AddCircle(screenPos, radius, color, 16, 2.0f);
            
            // 绘制物品名称（上方）
            ImVec2 nameSize = ImGui::CalcTextSize(actor.DisplayName.c_str());
            ImVec2 namePos(screenPos.x - nameSize.x / 2, screenPos.y - radius - nameSize.y - 2);
            drawList->AddText(namePos, color, actor.DisplayName.c_str());
            
            // 绘制距离（下方）
            char distText[32];
            sprintf_s(distText, "%.0fm", distanceMeters);
            ImVec2 distSize = ImGui::CalcTextSize(distText);
            ImVec2 distPos(screenPos.x - distSize.x / 2, screenPos.y + radius + 2);
            drawList->AddText(distPos, color, distText);
        }
    }
}
