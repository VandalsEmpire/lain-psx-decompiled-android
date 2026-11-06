/* re:Lain Wired Workstation: original ImGui theme/desktop illustration.
 * Launcher only. No game renderer or overlay changes. */
#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace wired {
inline ImU32 rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
inline void setup_style() {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 2.0f;
    s.PopupRounding = 1.0f;
    s.GrabRounding = 1.0f;
    s.ScrollbarRounding = 0.0f;
    s.WindowBorderSize = 2.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.WindowTitleAlign = ImVec2(0.06f, 0.5f);
    s.WindowPadding = ImVec2(11, 9);
    s.FramePadding = ImVec2(9, 7);
    s.ItemSpacing = ImVec2(9, 7);
    s.ScrollbarSize = 17.0f;
    s.Colors[ImGuiCol_Text]              = ImVec4(.91f, .92f, .98f, 1);
    s.Colors[ImGuiCol_TextDisabled]      = ImVec4(.62f, .69f, .82f, 1);
    s.Colors[ImGuiCol_WindowBg]          = ImVec4(.13f, .17f, .29f, .99f);
    s.Colors[ImGuiCol_ChildBg]           = ImVec4(.09f, .13f, .22f, .96f);
    s.Colors[ImGuiCol_PopupBg]           = ImVec4(.19f, .23f, .38f, 1);
    s.Colors[ImGuiCol_Border]            = ImVec4(.64f, .69f, .86f, 1);
    s.Colors[ImGuiCol_BorderShadow]      = ImVec4(.015f, .02f, .06f, 1);
    s.Colors[ImGuiCol_FrameBg]           = ImVec4(.32f, .36f, .51f, 1);
    s.Colors[ImGuiCol_FrameBgHovered]    = ImVec4(.45f, .48f, .67f, 1);
    s.Colors[ImGuiCol_FrameBgActive]     = ImVec4(.51f, .50f, .77f, 1);
    s.Colors[ImGuiCol_TitleBg]           = ImVec4(.19f, .24f, .39f, 1);
    s.Colors[ImGuiCol_TitleBgActive]     = ImVec4(.30f, .36f, .57f, 1);
    s.Colors[ImGuiCol_TitleBgCollapsed]  = ImVec4(.14f, .18f, .27f, 1);
    s.Colors[ImGuiCol_MenuBarBg]         = ImVec4(.49f, .51f, .67f, 1);
    s.Colors[ImGuiCol_ScrollbarBg]       = ImVec4(.13f, .17f, .27f, 1);
    s.Colors[ImGuiCol_ScrollbarGrab]     = ImVec4(.47f, .48f, .66f, 1);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(.58f, .59f, .77f, 1);
    s.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(.69f, .71f, .89f, 1);
    s.Colors[ImGuiCol_CheckMark]         = ImVec4(.47f, .94f, .58f, 1);
    s.Colors[ImGuiCol_SliderGrab]        = ImVec4(.64f, .65f, .86f, 1);
    s.Colors[ImGuiCol_SliderGrabActive]  = ImVec4(.79f, .79f, 1, 1);
    s.Colors[ImGuiCol_Button]            = ImVec4(.45f, .48f, .65f, 1);
    s.Colors[ImGuiCol_ButtonHovered]     = ImVec4(.56f, .58f, .81f, 1);
    s.Colors[ImGuiCol_ButtonActive]      = ImVec4(.33f, .34f, .56f, 1);
    s.Colors[ImGuiCol_Header]            = ImVec4(.30f, .34f, .50f, 1);
    s.Colors[ImGuiCol_HeaderHovered]     = ImVec4(.49f, .51f, .69f, 1);
    s.Colors[ImGuiCol_HeaderActive]      = ImVec4(.62f, .60f, .84f, 1);
    s.Colors[ImGuiCol_Separator]         = ImVec4(.58f, .64f, .83f, 1);
    s.Colors[ImGuiCol_SeparatorHovered]  = ImVec4(.65f, .66f, .87f, 1);
    s.Colors[ImGuiCol_SeparatorActive]   = ImVec4(.70f, .69f, .95f, 1);
    s.Colors[ImGuiCol_ResizeGrip]        = ImVec4(.65f, .67f, .89f, .5f);
    s.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(.75f, .75f, .96f, .8f);
    s.Colors[ImGuiCol_ResizeGripActive]  = ImVec4(.80f, .81f, 1, 1);
    s.Colors[ImGuiCol_Tab]               = ImVec4(.27f, .29f, .44f, 1);
    s.Colors[ImGuiCol_TabHovered]        = ImVec4(.58f, .58f, .80f, 1);
    s.Colors[ImGuiCol_TabSelected]       = ImVec4(.43f, .46f, .69f, 1);
    s.Colors[ImGuiCol_ModalWindowDimBg]  = ImVec4(.03f, .04f, .10f, .70f);
}
inline void bevel(ImDrawList *d, ImVec2 a, ImVec2 b, bool bright = true) {
    const ImU32 light = rgba(202, 207, 235), dark = rgba(22, 26, 54);
    d->AddLine(a, ImVec2(b.x, a.y), bright ? light : dark, 2.0f);
    d->AddLine(a, ImVec2(a.x, b.y), bright ? light : dark, 2.0f);
    d->AddLine(ImVec2(a.x, b.y), b, bright ? dark : light, 2.0f);
    d->AddLine(ImVec2(b.x, a.y), b, bright ? dark : light, 2.0f);
}
inline void android_robot(ImDrawList *d, float x, float y, float k) {
    const ImU32 green = rgba(137, 211, 79);
    d->AddLine(ImVec2(x + 6*k,y + 5*k), ImVec2(x + 2*k,y), green, 1.8f*k);
    d->AddLine(ImVec2(x + 14*k,y + 5*k), ImVec2(x + 18*k,y), green, 1.8f*k);
    d->AddRectFilled(ImVec2(x+2*k,y+5*k), ImVec2(x+18*k,y+13*k), green, 6*k);
    d->AddRectFilled(ImVec2(x+2*k,y+12*k), ImVec2(x+18*k,y+23*k), green, 1*k);
    d->AddRectFilled(ImVec2(x-1*k,y+13*k), ImVec2(x+1*k,y+21*k), green, 1*k);
    d->AddRectFilled(ImVec2(x+19*k,y+13*k), ImVec2(x+21*k,y+21*k), green, 1*k);
    d->AddRectFilled(ImVec2(x+4*k,y+21*k), ImVec2(x+7*k,y+26*k), green, 1*k);
    d->AddRectFilled(ImVec2(x+13*k,y+21*k), ImVec2(x+16*k,y+26*k), green, 1*k);
    d->AddCircleFilled(ImVec2(x+7*k,y+9*k), 1*k, rgba(21,40,36));
    d->AddCircleFilled(ImVec2(x+13*k,y+9*k), 1*k, rgba(21,40,36));
}
inline void desktop(ImVec2 size, float scale) {
    ImDrawList *d = ImGui::GetBackgroundDrawList();
    const float w = size.x, h = size.y;
    d->AddRectFilledMultiColor(ImVec2(0,0), ImVec2(w,h), rgba(33,33,65),rgba(20,24,50),rgba(12,17,39),rgba(44,38,77));
    // Deliberately abstract network/utility-pole background, without packaged game art.
    for (int i = 0; i < 7; ++i) {
        const float x = w * (.06f + i * .17f);
        const float top = h * (.06f + (i % 3) * .08f);
        const float bottom = h * .94f;
        const ImU32 ink = rgba(11,14,35,155);
        d->AddLine(ImVec2(x,top), ImVec2(x-20,bottom), ink, (5.0f + (i%3))*scale);
        d->AddLine(ImVec2(x-29,top+h*.12f),ImVec2(x+27,top+h*.12f),ink,3.0f*scale);
        d->AddLine(ImVec2(x-38,top+h*.18f),ImVec2(x+29,top+h*.18f),ink,2.0f*scale);
        for (int j=0;j<5;++j) {
            float xx = w*(j+1)/6.0f;
            d->AddLine(ImVec2(x, top+h*.12f),ImVec2(xx, top+h*.20f + 6.0f*std::sin(i+j)), rgba(96,89,144,65),1.0f);
        }
    }
    // Faint wired graph behind the windows.
    const float ax[]={.48f,.55f,.63f,.71f,.78f,.86f,.92f,.82f,.67f};
    const float ay[]={.12f,.07f,.14f,.09f,.16f,.08f,.19f,.26f,.24f};
    const int edges[][2]={{0,1},{0,2},{1,3},{2,4},{3,5},{4,6},{4,7},{6,7},{7,8},{8,2}};
    for (const auto &e: edges) d->AddLine(ImVec2(ax[e[0]]*w,ay[e[0]]*h),ImVec2(ax[e[1]]*w,ay[e[1]]*h),rgba(143,141,226,125),1.4f*scale);
    for (int i=0;i<9;++i) {
        ImVec2 p(ax[i]*w, ay[i]*h);
        d->AddRectFilled(ImVec2(p.x-5*scale,p.y-5*scale), ImVec2(p.x+5*scale,p.y+5*scale),rgba(28,30,63));
        d->AddRect(ImVec2(p.x-5*scale,p.y-5*scale), ImVec2(p.x+5*scale,p.y+5*scale),rgba(145,147,233),0,0,1.3f*scale);
    }
    for (int y = 2; y < (int)h; y += 5) d->AddLine(ImVec2(0,(float)y), ImVec2(w,(float)y),rgba(8,10,23,20),1);
    d->AddText(ImVec2(w*.77f,h*.065f), rgba(177,179,225,170), "t h e  w i r e d _");

    const float t=27*scale, b=34*scale;
    d->AddRectFilledMultiColor(ImVec2(0,0),ImVec2(w,t),rgba(187,187,212),rgba(174,174,206),rgba(111,119,157),rgba(135,138,181));
    bevel(d, ImVec2(0,0), ImVec2(w,t));
    d->AddRectFilledMultiColor(ImVec2(0,h-b),ImVec2(w,h),rgba(184,188,217),rgba(176,179,206),rgba(104,114,149),rgba(115,123,166));
    bevel(d,ImVec2(0,h-b),ImVec2(w,h));
    d->AddText(ImVec2(13*scale,6*scale),rgba(24,25,48),"re:Lain");
    char clock[64] = "";
    std::time_t now = std::time(nullptr);
    std::tm *tm = std::localtime(&now);
    if (tm) std::strftime(clock,sizeof clock,"%Y-%m-%d  %H:%M",tm);
    ImVec2 clock_size=ImGui::CalcTextSize(clock);
    d->AddText(ImVec2(w-clock_size.x-16*scale,6*scale),rgba(29,31,53),clock);
    android_robot(d, 10*scale,h-b+4*scale,0.92f*scale);
}
/* Purely decorative, slow floating Wired nodes and traveling signal pulses.
 * ImGui::GetTime() runs only while the launcher renders; no threads/timers.
 * Keeps a fixed 8-node topology even as nodes move a few pixels. */
inline void graph(const ImVec2 size) {
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    ImDrawList *d=ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x+size.x, origin.y+size.y);
    d->AddRectFilled(origin,end,rgba(16,24,40));
    d->AddRect(origin,end,rgba(119,133,178));
    const float px[]={.08f,.26f,.39f,.51f,.72f,.89f,.82f,.61f};
    const float py[]={.59f,.30f,.75f,.49f,.19f,.42f,.73f,.70f};
    const int edge[][2]={{0,1},{0,2},{1,3},{2,3},{3,4},{3,6},{4,5},{5,6},{6,7},{7,2}};
    const double t=ImGui::GetTime();
    ImVec2 nodes[8];
    const float drift_x=std::min(6.0f, size.x * .017f);
    const float drift_y=std::min(4.0f, size.y * .06f);
    for (int i=0; i<8; ++i) {
        nodes[i] = ImVec2(origin.x + size.x * px[i] + drift_x * std::sin(t*.48+i*1.71),
                          origin.y + size.y * py[i] + drift_y * std::cos(t*.37+i*2.13));
    }
    for (int i=0; i<10; ++i) {
        const ImVec2 a=nodes[edge[i][0]], b=nodes[edge[i][1]];
        d->AddLine(a,b,rgba(93,201,143),1.25f);
        // One low-key signal running along each edge at a phase offset.
        const float phase=std::fmod((float)t*.12f + i*.23f,1.0f);
        const ImVec2 pulse(a.x+(b.x-a.x)*phase,a.y+(b.y-a.y)*phase);
        d->AddCircleFilled(pulse,2.0f,rgba(196,246,213,220));
    }
    for (int i=0; i<8; ++i) {
        const ImVec2 p=nodes[i];
        d->AddRectFilled(ImVec2(p.x-4,p.y-4),ImVec2(p.x+4,p.y+4),rgba(71,194,100));
        d->AddRect(ImVec2(p.x-4,p.y-4),ImVec2(p.x+4,p.y+4),rgba(169,245,184));
    }
    ImGui::Dummy(size);
}
inline void hero_background(float width, float height) {
    const ImVec2 p=ImGui::GetWindowPos();
    ImDrawList *d=ImGui::GetWindowDrawList();
    const ImVec2 q(p.x+width,p.y+height);
    d->AddRectFilledMultiColor(p,q,rgba(22,25,47),rgba(34,32,70),rgba(15,23,45),rgba(38,31,76));
    // Lines form a subdued, abstract portrait-like profile; users can replace with their own art later.
    const ImVec2 c(p.x+width*.69f,p.y+height*.46f);
    const float rx=width*.22f, ry=height*.35f;
    d->AddEllipse(c,ImVec2(rx,ry),rgba(124,118,188,112),0,48,2);
    for(int i=0;i<17;i++) {
        const float f=(float)i/17;
        const float start=c.x-rx+f*rx*2;
        const float end=start-35*std::sin(f*3.14f);
        d->AddLine(ImVec2(start,p.y+height*.08f),ImVec2(end,p.y+height*.72f),rgba(100,90,165,65),1);
    }
    d->AddLine(ImVec2(c.x-rx*.65f,c.y-ry*.08f),ImVec2(c.x+rx*.08f,c.y-ry*.11f),rgba(175,172,217,140),2);
    d->AddCircleFilled(ImVec2(c.x-rx*.16f,c.y-ry*.09f),3.2f,rgba(198,192,241,160));
    for(int i=0;i<20;i++) {
        float y=p.y + (float)i*height/20;
        d->AddLine(ImVec2(p.x,y),ImVec2(q.x,y),rgba(0,4,23,25),1);
    }
}
} // namespace wired
