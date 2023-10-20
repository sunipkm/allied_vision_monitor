#pragma once

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include <VmbC/VmbC.h>

#include <mutex>

#include <math.h>

/*
#define eprintf(fmt, ...)                                                                 \
    {                                                                                     \
        fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
        fflush(stderr);                                                                   \
    }
*/
#define eprintf(fmt, ...)

class Image
{
private:
    uint32_t width;
    uint32_t height;
    uint32_t nelem;
    uint8_t *data;
    VmbPixelFormat_t pixelFormat;
    GLuint texture = 0;
    GLenum fmt;
    GLenum type;
    bool reset = false;
    bool newdata = false;
    std::mutex mtx;

public:
    Image()
    {
        width = 0;
        height = 0;
        nelem = 0;
        texture = 0;
        fmt = GL_LUMINANCE;
        type = GL_UNSIGNED_BYTE;
        pixelFormat = VmbPixelFormatMono8;
    }

    ~Image()
    {
        if (texture)
            glDeleteTextures(1, &texture);
    }

    void get_texture(GLuint &tex, uint32_t &w, uint32_t &h) // draw in the ImGui thread
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (!newdata)
        {
            tex = texture;
            w = width;
            h = height;
            return;
        }
        newdata = false;
        // scale data
        if (type == GL_UNSIGNED_SHORT) // 16 bits data
        {
            uint16_t *_data = (uint16_t *)data;
            for (size_t i = 0; i < width * height; i++)
                _data[i] = _data[i] << nelem;
        }
        if (reset)
        {
            pixfmt_to_glfmt(pixelFormat, nelem, fmt, type);
            eprintf("Image: %u x %u | %u | %u | %u\n", width, height, pixelFormat, fmt, type);
            if (texture)
            {
                glDeleteTextures(1, &texture);
                eprintf("Deleted texture: %d\n", texture);
            }
            GLuint itexture;
            glGenTextures(1, &itexture);
            eprintf("Generated texture: %d\n", itexture);
            glBindTexture(GL_TEXTURE_2D, itexture);
            eprintf("Bound texture: %d\n", itexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, type, data);
            eprintf("Set texture: %d | %u x %u\n", itexture, width, height);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            eprintf("Created texture: %d | %u x %u\n", itexture, width, height);
            texture = itexture;
            reset = false;
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            eprintf("Rebound texture: %d\n", texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, fmt, type, data);
            eprintf("Updated texture: %d | %u x %u\n", texture, width, height);
        }
        tex = texture;
        w = width;
        h = height;
    }

    void update(VmbFrame_t *frame)
    {
        if (data == frame->buffer)
        {
            // reading from this frame, lock!
            std::lock_guard<std::mutex> lock(mtx);
            // now do stuff and get out
            unsafe_update(frame);
            return;
        }
        // not reading from this frame, check if it is rendering
        else if (!mtx.try_lock())
        {
            return; // rendering, so don't update
        }
        // not rendering, so update
        eprintf("Updating image\n");
        unsafe_update(frame);
        mtx.unlock();
    }

private:
    void unsafe_update(VmbFrame_t *frame)
    {
        if (width != frame->width || height != frame->height || pixelFormat != frame->pixelFormat)
        {
            reset = true;
        }
        width = frame->width;
        height = frame->height;
        pixelFormat = frame->pixelFormat;
        data = (uint8_t *)frame->buffer;
        newdata = true;
    }

    static void update(Image *self, VmbFrame_t *frame)
    {
        self->update(frame);
    }

    static void pixfmt_to_glfmt(VmbPixelFormat_t pfmt, uint32_t &nelem, GLenum &fmt, GLenum &type)
    {
        nelem = 0;
        switch (pfmt)
        {
        case VmbPixelFormatMono8:
            fmt = GL_LUMINANCE;
            type = GL_UNSIGNED_BYTE;
            break;
        case VmbPixelFormatMono10:
            fmt = GL_LUMINANCE;
            type = GL_UNSIGNED_SHORT;
            nelem = 6;
            break;
        case VmbPixelFormatMono12:
            fmt = GL_LUMINANCE;
            type = GL_UNSIGNED_SHORT;
            nelem = 4;
            break;
        case VmbPixelFormatMono14:
            fmt = GL_LUMINANCE;
            type = GL_UNSIGNED_SHORT;
            nelem = 2;
            break;
        case VmbPixelFormatMono16:
            fmt = GL_LUMINANCE;
            type = GL_UNSIGNED_SHORT;
            break;
        case VmbPixelFormatBgr8:
            fmt = GL_BGR;
            type = GL_UNSIGNED_BYTE;
            break;
        case VmbPixelFormatBgra8:
            fmt = GL_BGRA;
            type = GL_UNSIGNED_BYTE;
            break;
        case VmbPixelFormatRgb8:
            fmt = GL_RGB;
            type = GL_UNSIGNED_BYTE;
            break;
        case VmbPixelFormatRgba8:
            fmt = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
            break;
        case VmbPixelFormatRgb16:
            fmt = GL_RGB;
            type = GL_UNSIGNED_SHORT;
            break;
        case VmbPixelFormatBgr16:
            fmt = GL_BGR;
            type = GL_UNSIGNED_SHORT;
            break;
        case VmbPixelFormatRgba16:
            fmt = GL_RGBA;
            type = GL_UNSIGNED_SHORT;
            break;
        case VmbPixelFormatBgra16:
            fmt = GL_BGRA;
            type = GL_UNSIGNED_SHORT;
            break;
        default:
            fmt = GL_LUMINANCE;
            type = GL_UNSIGNED_BYTE;
            nelem = 0;
            break;
        }
    }
};
