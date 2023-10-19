#pragma once
#include <iostream>
#include "imgui/imgui.h"
#include <stdio.h>
#include <math.h>
#include <chrono>
#include <mutex>

#include <alliedcam.h>

#include "string_format.hpp"

#include "imagetexture.hpp"

class CaptureStat
{
private:
    std::chrono::steady_clock::time_point last;
    bool firstrun = true;
    double avg = 0, avg2 = 0;
    uint64_t count = 0;
    std::mutex mtx;

    void update_avg(double period)
    {
        uint64_t ncount = count++;
        avg = (avg * ncount + period) / (count);
        avg2 = (avg2 * ncount + period * period) / (count);
    }

public:
    void reset()
    {
        std::lock_guard<std::mutex> lock(mtx);
        firstrun = true;
        avg = 0;
        avg2 = 0;
        count = 0;
    }

    void update()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (firstrun)
        {
            last = std::chrono::steady_clock::now();
            firstrun = false;
        }
        else
        {
            auto now = std::chrono::steady_clock::now();
            auto diff = now - last;
            auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(diff);
            double period = diff_us.count();
            update_avg(period);
            last = now;
        }
    }

    void get_stats(double &avg, double &stddev)
    {
        avg = this->avg;
        stddev = sqrt(avg2 - avg * avg);
    }
};

class CameraInfo
{
public:
    std::string idstr;
    std::string name;
    std::string model;
    std::string serial;

    CameraInfo(CameraInfo &other)
    {
        idstr = other.idstr;
        name = other.name;
        model = other.model;
        serial = other.serial;
    }

    CameraInfo()
    {
        idstr = "";
        name = "";
        model = "";
        serial = "";
    }

    CameraInfo(CameraInfo *other)
    {
        idstr = other->idstr;
        name = other->name;
        model = other->model;
        serial = other->serial;
    }

    CameraInfo(VmbCameraInfo_t info)
    {
        idstr = info.cameraIdString;
        name = info.cameraName;
        model = info.modelName;
        serial = info.serialString;
    }
};

static void Callback(const AlliedCameraHandle_t handle, const VmbHandle_t stream, VmbFrame_t *frame, void *user_data);

class ImageDisplay
{
private:
    ImVec2 render_size(uint32_t swid, uint32_t shgt)
    {
        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        float wid = avail_size[0]; // get window width
        float hgt = avail_size[1]; // get window height
        ImVec2 out = ImVec2(
            wid,
            round((float)shgt / (float)swid * wid) // calculate height
        );
        if (out[1] > hgt)
        {
            out[1] = hgt;
            out[0] = round((float)swid / (float)shgt * hgt); // calculate width
        }
        return out;
    }

public:
    bool show;
    CameraInfo info;
    std::string title;
    bool opened;
    AlliedCameraHandle_t handle = nullptr;
    std::string errmsg;
    Image img;
    CaptureStat stat;

    ImageDisplay(const CameraInfo &info)
    {
        show = false;
        this->info = info;
        title = info.name + " [" + info.serial + "]";
        opened = false;
        VmbError_t err = allied_open_camera(&handle, info.idstr.c_str());
        if (err != VmbErrorSuccess)
        {
            errmsg = "Could not open camera: " + std::string(allied_strerr(err));
            return;
        }
        err = allied_alloc_framebuffer(handle, 5);
        if (err != VmbErrorSuccess)
        {
            update_err(string_format("Could not allocate memory for %d frames", 5), err);
            return;
        }
        opened = true;
    }

    void display()
    {
        static bool size_changed = true;
        static bool ofst_changed = true;
        static bool exp_changed = true;
        static bool pressed_start = false;
        static bool pressed_stop = false;

        static int swid, shgt;
        static int ofx, ofy;
        static double expmin, expmax, expstep;
        static double currexp;
        static double frate;

        ImGui::SetNextWindowSizeConstraints(ImVec2(512, 512), ImVec2(INFINITY, INFINITY));
        const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        if (show && ImGui::Begin(title.c_str(), &show))
        {
            if (!opened)
            {
                ImGui::TextUnformatted(errmsg.c_str());
            }
            else
            {
                VmbError_t err;
                bool capturing = allied_camera_acquiring(handle) || allied_camera_streaming(handle);
                // set width + height
                {
                    if (size_changed)
                    {
                        VmbInt64_t width, height;
                        err = allied_get_image_size(handle, &width, &height);
                        update_err("Size changed", err);
                        err = allied_get_acq_framerate(handle, &frate);
                        update_err("Get framerate", err);
                        swid = width;
                        shgt = height;
                        size_changed = false;
                    }
                    ImGui::Text("Image Size:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##width" + info.idstr).c_str(), &swid, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Text(" x ");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##height" + info.idstr).c_str(), &shgt, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Update##Size" + info.idstr).c_str()) && !capturing)
                    {
                        size_changed = true;
                        err = allied_set_image_size(handle, swid, shgt);
                        if (err != VmbErrorSuccess)
                        {
                            errmsg = string_format("Could not set image size to %u x %u: ", swid, shgt) + std::string(allied_strerr(err));
                        }
                    }
                }
                // set offset
                {
                    if (ofst_changed)
                    {
                        VmbInt64_t width, height;
                        err = allied_get_image_ofst(handle, &width, &height);
                        ofx = width;
                        ofy = height;
                        ofst_changed = false;
                    }
                    ImGui::Text("Image Offset:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##ofstx" + info.idstr).c_str(), &ofx, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Text(" x ");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 5);
                    ImGui::InputInt(("##ofsty" + info.idstr).c_str(), &ofy, 0, 0, capturing ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Update##Ofst" + info.idstr).c_str()) && !capturing)
                    {
                        ofst_changed = true;
                        err = allied_set_image_ofst(handle, ofx, ofy);
                        if (err != VmbErrorSuccess)
                        {
                            errmsg = "Could not set image offset: " + std::string(allied_strerr(err));
                        }
                    }
                }
                // set exposure
                {
                    if (exp_changed)
                    {
                        err = allied_get_exposure_range_us(handle, &expmin, &expmax, &expstep);
                        update_err("Get exposure range", err);
                        err = allied_get_exposure_us(handle, &currexp);
                        update_err("Get exposure", err);
                        exp_changed = false;
                    }
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 25);
                    if (ImGui::InputDouble(("Exposure (us)##" + info.idstr).c_str(), &currexp, expstep, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        if (currexp < expmin)
                            currexp = expmin;
                        if (currexp > expmax)
                            currexp = expmax;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Update##Exposure" + info.idstr).c_str()))
                    {
                        if (currexp < expmin)
                            currexp = expmin;
                        if (currexp > expmax)
                            currexp = expmax;
                        err = allied_set_exposure_us(handle, currexp);
                        update_err("Update exposure", err);
                        exp_changed = true;
                        stat.reset();
                    }
                }
                // Start/stop capture
                if (!capturing)
                {
                    if (pressed_stop)
                        pressed_stop = false;
                    if (ImGui::Button(("Start Capture##" + info.idstr).c_str()) && !pressed_start)
                    {
                        pressed_start = true;
                        stat.reset();
                        err = allied_start_capture(handle, &Callback, (void *)this); // set the callback here
                        update_err("Start capture", err);
                    }
                }
                else
                {
                    if (pressed_start)
                        pressed_start = false;
                    if (ImGui::Button(("Stop Capture##" + info.idstr).c_str()) && !pressed_stop)
                    {
                        pressed_stop = true;
                        err = allied_stop_capture(handle);
                        update_err("Stop capture", err);
                    }
                }
                ImGui::Separator();
                // Capture stats display
                double avg, std;
                stat.get_stats(avg, std);
                ImGui::Text(
                    "Frame Time: %.3f +/- %.6f ms", avg * 1e-3, std * 1e-3);
                ImGui::Text("Frame Rate: %.3f FPS | Expected max: %.3f FPS", 1e6 / avg, frate);
                ImGui::Separator();
                // Error message display
                ImGui::Text("Last error: %s", errmsg.c_str());
                if (ImGui::Button(("Clear##" + info.idstr).c_str()))
                {
                    errmsg = "";
                }
                ImGui::Separator();
                // Image Display
                ImGui::Text("Image");
                GLuint texture = 0;
                uint32_t width = 0, height = 0;
                if (show)
                {
                    img.get_texture(texture, width, height);
                }
                ImGui::Text("Texture: %d | %u x %u | %d", texture, width, height, (int)ImGui::GetWindowWidth());
                if (show)
                {
                    ImGui::Image((void *)(intptr_t)texture, render_size(width, height));
                }
                ImGui::End();
            }
        }
    }

    void update_err(const char *where, VmbError_t err)
    {
        if (err != VmbErrorSuccess)
        {
            errmsg = std::string(where) + ": " + std::string(allied_strerr(err));
        }
    }

    void update_err(std::string where, VmbError_t err)
    {
        if (err != VmbErrorSuccess)
        {
            errmsg = where + ": " + std::string(allied_strerr(err));
        }
    }

    void cleanup()
    {
        if (opened)
        {
            allied_stop_capture(handle);  // just stop capture...
            allied_close_camera(&handle); // close the camera
            opened = false;
        }
    }

    ~ImageDisplay()
    {
        cleanup();
    }
};

static void Callback(const AlliedCameraHandle_t handle, const VmbHandle_t stream, VmbFrame_t *frame, void *user_data)
{
    assert(user_data);
    ImageDisplay *self = (ImageDisplay *)user_data;
    self->stat.update();
    self->img.update(frame);
}