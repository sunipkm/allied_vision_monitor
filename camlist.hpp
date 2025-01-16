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

#include "aDIO_library.h"

static std::map<uint32_t, int> adio_used;

static const char *adio_list[] = {
    "None",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
};

class CameraList
{
public:
    bool can_refresh = true;
    std::unordered_set<uint32_t> open_cams;
    std::map<uint32_t, ImageDisplay *> camstructs;
    std::map<uint32_t, CameraInfo> caminfos;
    std::string inp_id = "";
    std::string errstr = "";
    DeviceHandle adio_dev = nullptr;
    StringHasher *hashgen;
    bool win_debug_adio = false;

    void update_err(int devidx, const char *errmsg)
    {
        errstr = "Device " + std::to_string(devidx) + ": " + errmsg;
    }

    void update_err(int devidx, std::string &errmsg)
    {
        errstr = "Device " + std::to_string(devidx) + ": " + errmsg;
    }

    void update_err(int devidx, std::string errmsg)
    {
        errstr = "Device " + std::to_string(devidx) + ": " + errmsg;
    }

    void update_err(const char *errmsg)
    {
        errstr = errmsg;
    }

    void update_err(std::string &errmsg)
    {
        errstr = errmsg;
    }

    void update_err(std::string errmsg)
    {
        errstr = errmsg;
    }

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
                printf("Could not get camera info for %s: %s\n", inp_id.c_str(), allied_strerr(err));
                update_err(string_format("Could not get camera info for %s: %s\n", inp_id.c_str(), allied_strerr(err)));
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
            if (ids.find(val) != ids.end()) // already in there
            {
                continue;
            }
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
                camstructs[val] = new ImageDisplay(cf, adio_dev);
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

    CameraList(std::string id, DeviceHandle adio_dev)
    {
        this->adio_dev = adio_dev;
        this->inp_id = inp_id;
        hashgen = new StringHasher;
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
        static ImVec2 outer_size_value = ImVec2(0.0f, TEXT_BASE_HEIGHT * 15);
        static ImGuiTableFlags flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
        ImGui::SetNextWindowSizeConstraints(ImVec2(512, 512), ImVec2(INFINITY, INFINITY));
        ImGui::Begin("Camera List");
        if (camstructs.size() && ImGui::BeginTable("camera_table", 5, flags, outer_size_value))
        {
            ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Serial", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("ADIO", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);

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
                // combo
                if (ImGui::TableSetColumnIndex(3))
                {
                    bool capturing = win->running();
                    int oldsel = win->adio_bit + 1;
                    int sel = oldsel;
                    if (ImGui::Combo("", &sel, adio_list, IM_ARRAYSIZE(adio_list)) && !capturing)
                    {
                        if (adio_dev == nullptr) // not available, set to none
                        {
                            sel = 0;
                        }
                        if (sel != oldsel) // selection changed
                        {
                            auto entry = adio_used.find(oldsel - 1); // find the old selection
                            if (entry != adio_used.end()) // if found
                                adio_used.erase(entry);
                        }
                        if (sel > 0) // non zero selected
                        {
                            auto entry = adio_used.find(sel - 1); // find the new selection
                            if (entry != adio_used.end() && entry->second != row_id) // found this bit
                            {
                                sel = 0; // you can not select this, and you have already lost the one you had selected
                                update_err(row_id, string_format("Bit %d assigned to %d.", entry->first, entry->second)); // error message
                            }
                            else // did not find this bit, claim it
                            {
                                adio_used.insert({sel - 1, row_id});
                            }
                        }
                        win->adio_bit = sel - 1; // update selection
                        eprintlf("ADIO Sel: %d -> %d", oldsel, sel);
                    }
                }
                // buttons
                if (ImGui::TableSetColumnIndex(4))
                {
                    ImGui::PushID(row_id);
                    if (ImGui::SmallButton("Open"))
                    {
                        win->show = true;
                        open_cams.insert(id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Print ID"))
                    {
                        printf("Idx: %d | ID: %s\n", row_id, info.idstr.c_str());
                    }
                    ImGui::PopID();
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
        ImGui::SameLine();
        if (ImGui::Button("Start Capture All"))
        {
            for (auto it = open_cams.begin(); it != open_cams.end(); it++)
            {
                auto item = camstructs.at(*it);
                item->start_capture();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Capture All"))
        {
            for (auto it = open_cams.begin(); it != open_cams.end(); it++)
            {
                auto item = camstructs.at(*it);
                item->stop_capture();
            }
        }
        ImGui::Separator();
        if (errstr.length())
        {
            ImGui::Text("Error: %s", errstr.c_str());
        }
        if (ImGui::Button("Clear##ErrorMsg"))
        {
            errstr = "";
        }
        ImGui::Separator();
        if (adio_dev != nullptr)
        {
            ImGui::Checkbox("Debug ADIO", &win_debug_adio);
            ImGui::Separator();
        }
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::End();

        for (auto it = open_cams.begin(); it != open_cams.end(); it++)
        {
            auto item = camstructs.at(*it);
            item->display();
        }

        if (win_debug_adio)
        {
            // State
            static bool vals[8] = {false, false, false, false, false, false, false, false};
            // Draw ADIO debug window
            ImGui::Begin("ADIO Debug", &win_debug_adio);
            ImGui::Text("ADIO Minor Number: %d", adio_dev->minor);
            ImGui::Separator();
            uint8_t val;
            ReadPort_aDIO(adio_dev, 0, &val);
            ImGui::Text("ADIO Port 0: %01d %01d %01d %01d %01d %01d %01d %01d", (val >> 7) & 1, (val >> 6) & 1, (val >> 5) & 1, (val >> 4) & 1, (val >> 3) & 1, (val >> 2) & 1, (val >> 1) & 1, (val >> 0) & 1);
            for (int i = 0; i < 8; i++)
            {
                std::string label = "Port " + std::to_string(i);
                if (ImGui::Button(label.c_str()))
                {
                    vals[i] = !vals[i];
                    WriteBit_aDIO(adio_dev, 0, i, vals[i]);
                }
                ImGui::SameLine();
            }
            ImGui::Text(" ");
            ImGui::End();
        }
    }
};