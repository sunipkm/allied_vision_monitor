#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <algorithm>

#include "imgui/imgui.h"
#include <stdio.h>
#include <math.h>

#include <alliedcam.h>

#include "imagetexture.hpp"

#include "guiwin.hpp"

#include "stringhasher.hpp"

class CameraList
{
public:
    bool can_refresh = true;
    std::unordered_set<uint32_t> open_cams;
    std::map<uint32_t, ImageDisplay *> camstructs;
    std::map<uint32_t, CameraInfo> caminfos;
    std::string inp_id = "";
    StringHasher *hashgen;

    void refresh_list()
    {
        VmbError_t err;
        VmbCameraInfo_t *cams = nullptr;
        VmbUint32_t ct;
        if (inp_id.length() > 0)
        {
            cams = new VmbCameraInfo_t;
            err = VmbCameraInfoQuery(inp_id.c_str(), cams, sizeof(VmbCameraInfo_t));
            if (err != VmbErrorSuccess) // camera with ID not found
            {
                // log something
                return;
            }
            else
            {
                ct = 1; // we have info for only one camera
            }
        }
        else
        {
            err = allied_list_cameras(&cams, &ct);
            if (err != VmbErrorSuccess)
            {
                // log something
                return;
            }
        }
        // create cameras
        std::vector<VmbCameraInfo_t> cameras;
        cameras.assign(cams, cams + ct);
        // std::cout << "After assign: " << cameras.size() << std::endl;
        free(cams); // free the memory
        // std::cout << "After free: " << cameras.size() << std::endl;
        // create ID of cameras
        caminfos.clear();
        std::unordered_set<uint32_t> ids;
        for (auto cam = cameras.begin(); cam != cameras.end(); cam++)
        {
            uint32_t val = hashgen->get_hash(std::string(cam->cameraIdString));
            ids.insert(val);
            CameraInfo cf(*cam);
            caminfos[val] = cf;
        }
        // std::cout << "Cam infos: " << caminfos.size() << std::endl;
        // if there are no windows
        if (camstructs.size() == 0) // nothing in there, gotta make em
        {
            for (auto cinfo = caminfos.begin(); cinfo != caminfos.end(); cinfo++)
            {
                uint32_t val = cinfo->first;
                CameraInfo cf = cinfo->second;
                camstructs[val] = new ImageDisplay(cf);
            }
            // std::cout << "Cam structs size: " << camstructs.size() << std::endl;
            return;
        }
        // else find items to pop
        std::vector<uint32_t> popem;
        for (auto cstr = camstructs.begin(); cstr != camstructs.end(); cstr++)
        {
            uint32_t id = cstr->first;

            if (!(ids.find(id) != ids.end()) && (open_cams.find(id) != open_cams.end()))
            {
                popem.push_back(id);
            }
        }
        // pop em
        for (auto id = popem.begin(); id != popem.end(); id++)
        {
            camstructs.erase(*id);
        }
        // std::cout << "Cam structs size: " << camstructs.size() << std::endl;
    }

    CameraList(const char *id = nullptr)
    {
        hashgen = new StringHasher;
        if (id != nullptr)
        {
            inp_id = std::string(id); // store the ID
        }
        refresh_list();
    }

    ~CameraList()
    {
        // do something here
        delete hashgen;
        for (auto it = camstructs.begin(); it != camstructs.end(); it++)
        {
            delete it->second; // delete memory
        }
        camstructs.clear();
    }

    void render()
    {
        // const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
        static ImVec2 outer_size_value = ImVec2(0.0f, TEXT_BASE_HEIGHT * 12);
        static ImGuiTableFlags flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
        ImGui::SetNextWindowSizeConstraints(ImVec2(512, 512), ImVec2(INFINITY, INFINITY));
        ImGui::Begin("Camera List");
        if (camstructs.size() && ImGui::BeginTable("camera_table", 4, flags, outer_size_value))
        {
            ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Serial", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);

            ImGui::TableHeadersRow();


            int row_id = 0;
            ImGui::PushButtonRepeat(true);
            for (auto it = camstructs.begin(); it != camstructs.end(); it++)
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);
                row_id++;
                uint32_t id = it->first;
                ImageDisplay *win = it->second;
                CameraInfo info = caminfos.at(id);
                // check if it is still showing
                if (!win->show) // not showing so pop it out of open cams
                {
                    open_cams.erase(id);
                }
                // row ID
                char label[32];
                ImGui::TableSetColumnIndex(0);
                snprintf(label, 32, "%02d", row_id);
                ImGui::TextUnformatted(label);
                // name
                if (ImGui::TableSetColumnIndex(1))
                    ImGui::TextUnformatted(info.name.c_str());
                // serial
                if (ImGui::TableSetColumnIndex(2))
                    ImGui::TextUnformatted(info.serial.c_str());
                // button
                if (ImGui::TableSetColumnIndex(3))
                {
                    if (ImGui::SmallButton("Open"))
                    {
                        win->show = true;
                        open_cams.insert(id);
                    }
                }
            }
            ImGui::PopButtonRepeat();
            ImGui::EndTable();
        }
        else if (camstructs.size() == 0)
        {
            ImGui::Text("No cameras are available");
        }
        if (ImGui::Button("Refresh"))
        {
            refresh_list();
        }
        ImGui::Separator();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::End();

        for (auto it = open_cams.begin(); it != open_cams.end(); it++)
        {
            auto item = camstructs.at(*it);
            item->display();
        }
    }
};