#include "imgui/imgui_internal.h"

namespace ImGui 
{
    void CenteredSeparator(float width = 0)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;
        ImGuiContext& g = *GImGui;
        /* 
        // Commented out because it is not tested, but it should work, but it won't be centered
        ImGuiWindowFlags flags = 0;
        if ((flags & (ImGuiSeparatorFlags_Horizontal | ImGuiSeparatorFlags_Vertical)) == 0)
            flags |= (window->DC.LayoutType == ImGuiLayoutType_Horizontal) ? ImGuiSeparatorFlags_Vertical : ImGuiSeparatorFlags_Horizontal;
        IM_ASSERT(ImIsPowerOfTwo((int)(flags & (ImGuiSeparatorFlags_Horizontal | ImGuiSeparatorFlags_Vertical))));   // Check that only 1 option is selected
        if (flags & ImGuiSeparatorFlags_Vertical)
        {
            VerticalSeparator();
            return;
        }
        */

        // Horizontal Separator
        float x1, x2;
        if (window->DC.CurrentColumns == NULL && (width == 0))
        {
            // Span whole window
            ///x1 = window->Pos.x; // This fails with SameLine(); CenteredSeparator();
            // Nah, we have to detect if we have a sameline in a different way
            x1 = window->DC.CursorPos.x;
            x2 = x1 + window->Size.x;
        }
        else
        {
            // Start at the cursor
            x1 = window->DC.CursorPos.x;
            if (width != 0) {
                x2 = x1 + width;
            }
            else 
            {
                x2 = window->ClipRect.Max.x;
                // Pad right side of columns (except the last one)
                if (window->DC.CurrentColumns && (window->DC.CurrentColumns->Current < window->DC.CurrentColumns->Count - 1))
                    x2 -= g.Style.ItemSpacing.x;
            }
        }
        float y1 = window->DC.CursorPos.y + int(window->DC.CurrLineSize.y / 2.0f);
        float y2 = y1 + 1.0f;

        window->DC.CursorPos.x += width; //+ g.Style.ItemSpacing.x;

        if (!window->DC.GroupOffset.x && window->DC.ColumnsOffset.x == 0.0f)
            x1 += window->DC.Indent.x;

        const ImRect bb(ImVec2(x1, y1), ImVec2(x2, y2));
        ItemSize(ImVec2(0.0f, 0.0f)); // NB: we don't provide our width so that it doesn't get feed back into AutoFit, we don't provide height to not alter layout.
        if (!ItemAdd(bb, 0))
        {
            return;
        }

        window->DrawList->AddLine(bb.Min, ImVec2(bb.Max.x, bb.Min.y), GetColorU32(ImGuiCol_Border));

        /* // Commented out because LogText is hard to reach outside imgui.cpp
        if (g.LogEnabled)
        LogText(IM_NEWLINE "--------------------------------");
        */
    }

    // Create a centered separator right after the current item.
    // Eg.: 
    // ImGui::PreSeparator(10);
    // ImGui::Text("Section VI");
    // ImGui::SameLineSeparator();
    void SameLineSeparator(float width = 0) {
        ImGui::SameLine();
        CenteredSeparator(width);
    }

    // Create a centered separator which can be immediately followed by a item
    void PreSeparator(float width) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->DC.CurrLineSize.y == 0)
            window->DC.CurrLineSize.y = ImGui::GetTextLineHeight();
        CenteredSeparator(width);
        ImGui::SameLine();
    }

    // The value for width is arbitrary. But it looks nice.
    void TextSeparator(char* text, float pre_width = 10.0f) 
    {
        ImGui::PreSeparator(pre_width);
        ImGui::Text("%s", text);
        ImGui::SameLineSeparator();
    }

    void test_fancy_separator() 
    {
        ImGuiIO io = ImGui::GetIO();
        static float t = 0.0f;
        t += io.DeltaTime;
        float f = sinf(4 * t * 3.14f / 9.0f) * sinf(4 * t * 3.14f / 7.0f);
        ImGui::PreSeparator(20 + 100 * abs(f));
        ImGui::TextColored(ImColor(0.6f, 0.3f, 0.3f, 1.0f), "Fancy separators");
        ImGui::SameLineSeparator();
        ImGui::Bullet();
        ImGui::CenteredSeparator(100);
        ImGui::SameLine();
        ImGui::Text("Centered separator");
        ImGui::Columns(2);
        ImGui::PreSeparator(10);
        ImGui::Text("Separator");
        ImGui::SameLineSeparator();
        ImGui::CenteredSeparator();
        ImGui::Text("Column 1");
        ImGui::SameLineSeparator();

        ImGui::NextColumn();

        ImGui::PreSeparator(10);
        ImGui::Text("The Same Separator");
        ImGui::SameLineSeparator();
        ImGui::CenteredSeparator();
        ImGui::Text("Column 2");
        ImGui::SameLineSeparator();

        ImGui::Columns(1);
        ImGui::TextSeparator((char *)"So decorative");
        ImGui::CenteredSeparator();
    }
}