/*
	Copyright 2019 flyinghead

	This file is part of reicast.

    reicast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    reicast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with reicast.  If not, see <https://www.gnu.org/licenses/>.
 */
#if defined(__ANDROID__) || defined(TARGET_IPHONE)

#include "gui_android.h"
#include "gui.h"
#include "stdclass.h"
#include "imgui.h"
#include "gui_util.h"
#include "rend/osd.h"
#include "imgui_driver.h"
#include "input/gamepad.h"
#include "input/gamepad_device.h"
#include "oslib/resources.h"
#include "cfg/cfg.h"
#include "input/gamepad.h"
#include <stb_image.h>

namespace vgamepad
{

static void loadLayout();

struct Control
{
	Control() = default;
	Control(float x, float y, float w = 64.f, float h = 64.f)
		: pos(x, y), size(w, h), uv0(0, 0), uv1(1, 1) {}

	ImVec2 pos;
	ImVec2 size;
	ImVec2 uv0;
	ImVec2 uv1;
};
static Control Controls[_Count];
static bool Visible = true;
static float AlphaTrans = 1.f;
static ImVec2 StickPos;	// analog stick position [-1, 1]
constexpr char const *BTN_PATH = "picture/buttons.png";

void displayCommands()
{
	draw();
    centerNextWindow();

    ImGui::Begin("##vgamepad", NULL, ImGuiWindowFlags_NoDecoration);

	if (ImGui::Button("Save", ScaledVec2(150, 50)))
	{
		stopEditing(false);
		gui_setState(GuiState::Settings);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset", ScaledVec2(150, 50)))
	{
		resetEditing();
		startEditing();
		gui_setState(GuiState::VJoyEdit);
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel", ScaledVec2(150, 50)))
	{
		stopEditing(true);
		gui_setState(GuiState::Settings);
	}
    ImGui::End();
}

static u8 *loadOSDButtons(int &width, int &height)
{
	int n;
	stbi_set_flip_vertically_on_load(1);

	FILE *file = nowide::fopen(get_readonly_data_path("buttons.png").c_str(), "rb");
	if (file == nullptr)
		// also try the home folder (android)
		file = nowide::fopen(get_readonly_config_path("buttons.png").c_str(), "rb");
	u8 *image_data = nullptr;
	if (file != nullptr)
	{
		image_data = stbi_load_from_file(file, &width, &height, &n, STBI_rgb_alpha);
		std::fclose(file);
	}
	if (image_data == nullptr)
	{
		size_t size;
		std::unique_ptr<u8[]> data = resource::load(BTN_PATH, size);
		image_data = stbi_load_from_memory(data.get(), (int)size, &width, &height, &n, STBI_rgb_alpha);
	}
	return image_data;
}

class ImguiVGamepadTexture : public ImguiTexture
{
public:
	ImTextureID getId() override
	{
		ImTextureID id = imguiDriver->getTexture(BTN_PATH);
		if (id == ImTextureID())
		{
			int width, height;
			u8 *imgData = loadOSDButtons(width, height);
			if (imgData != nullptr)
			{
				try {
					id = imguiDriver->updateTextureAndAspectRatio(BTN_PATH, imgData, width, height, nearestSampling);
				} catch (...) {
					// vulkan can throw during resizing
				}
				free(imgData);
			}
		}
		return id;
	}
};

constexpr float vjoy_sz[2][14] = {
	// L  U  R  D   X  Y  B  A  St  LT RT  Ana Stck FF
	{ 64,64,64,64, 64,64,64,64, 64, 90,90, 128, 64, 64 },
	{ 64,64,64,64, 64,64,64,64, 64, 64,64, 128, 64, 64 },
};

constexpr float OSD_TEX_W = 512.f;
constexpr float OSD_TEX_H = 256.f;

static void setUV()
{
	float u = 0;
	float v = 0;
	int i = 0;

	for (auto& control : Controls)
	{
		control.uv0.x = (u + 1) / OSD_TEX_W;
		control.uv0.y = 1.f - (v + 1) / OSD_TEX_H;
		control.uv1.x = (u + vjoy_sz[0][i] - 1) / OSD_TEX_W;
		control.uv1.y = 1.f - (v + vjoy_sz[1][i] - 1) / OSD_TEX_H;

		u += vjoy_sz[0][i];
		if (u >= OSD_TEX_W) {
			u -= OSD_TEX_W;
			v += vjoy_sz[1][i];
		}
		i++;
	}
}
static OnLoad _(&setUV);

void show() {
	Visible = true;
}

void hide() {
	Visible = false;
}

void setPosition(ControlId id, float x, float y, float w, float h)
{
	verify(id >= 0 && id < _VisibleCount);
	auto& control = Controls[id];
	control.pos.x = x;
	control.pos.y = y;
	if (w != 0)
		control.size.x = w;
	if (h != 0)
		control.size.y = h;
}

ControlId hitTest(float x, float y)
{
	for (const auto& control : Controls)
		if (x >= control.pos.x && x < control.pos.x + control.size.x
			&& y >= control.pos.y && y < control.pos.y + control.size.y)
			return static_cast<ControlId>(&control - &Controls[0]);
	return None;
}

u32 controlToDcKey(ControlId control)
{
	switch (control)
	{
		case Left: return DC_DPAD_LEFT;
		case Up: return DC_DPAD_UP;
		case Right: return DC_DPAD_RIGHT;
		case Down: return DC_DPAD_DOWN;
		case X: return DC_BTN_X;
		case Y: return DC_BTN_Y;
		case B: return DC_BTN_B;
		case A: return DC_BTN_A;
		case Start: return DC_BTN_START;
		case LeftTrigger: return DC_AXIS_LT;
		case RightTrigger: return DC_AXIS_RT;
		case FastForward: return EMU_BTN_FFORWARD;
		case LeftUp: return DC_DPAD_LEFT | DC_DPAD_UP;
		case RightUp: return DC_DPAD_RIGHT | DC_DPAD_UP;
		case LeftDown: return DC_DPAD_LEFT | DC_DPAD_DOWN;
		case RightDown: return DC_DPAD_RIGHT | DC_DPAD_DOWN;
		default: return 0;
	}
}

void setAnalogStick(float x, float y) {
	StickPos.x = x;
	StickPos.y = y;
}

float getControlWidth(ControlId control) {
	return Controls[control].size.x;	
}

static void drawButtonDim(ImDrawList *drawList, const Control& control, int state)
{
	float scale_h = settings.display.height / 480.f;
	float offs_x = (settings.display.width - scale_h * 640.f) / 2.f;
	ImVec2 pos = control.pos * scale_h;
	ImVec2 size = control.size * scale_h;
	pos.x += offs_x;
	if (static_cast<ControlId>(&control - &Controls[0]) == AnalogStick)
		pos += StickPos * size;

	float col = (0.5f - 0.25f * state / 255) * AlphaTrans;
	float alpha = (100.f - config::VirtualGamepadTransparency) / 100.f * AlphaTrans;
	ImVec4 color(col, col, col, alpha);

    ImguiVGamepadTexture tex;
	tex.draw(drawList, pos, size, control.uv0, control.uv1, color);
}

static void drawButton(ImDrawList *drawList, const Control& control, bool state) {
	drawButtonDim(drawList, control, state ? 0 : 255);
}

void draw()
{
#ifndef __ANDROID__
	if (Controls[Left].pos.x == 0.f)
	{
		loadLayout();
		if (Controls[Left].pos.x == 0.f)
			// mark done
			Controls[Left].pos.x = 1e-12f;
	}
#endif
		
	ImDrawList *drawList = ImGui::GetBackgroundDrawList();
	drawButton(drawList, Controls[Left], kcode[0] & DC_DPAD_LEFT);
	drawButton(drawList, Controls[Up], kcode[0] & DC_DPAD_UP);
	drawButton(drawList, Controls[Right], kcode[0] & DC_DPAD_RIGHT);
	drawButton(drawList, Controls[Down], kcode[0] & DC_DPAD_DOWN);

	drawButton(drawList, Controls[X], kcode[0] & (settings.platform.isConsole() ? DC_BTN_X : DC_BTN_C));
	drawButton(drawList, Controls[Y], kcode[0] & (settings.platform.isConsole() ? DC_BTN_Y : DC_BTN_X));
	drawButton(drawList, Controls[B], kcode[0] & DC_BTN_B);
	drawButton(drawList, Controls[A], kcode[0] & DC_BTN_A);

	drawButton(drawList, Controls[Start], kcode[0] & DC_BTN_START);

	drawButtonDim(drawList, Controls[LeftTrigger], lt[0] >> 8);

	drawButtonDim(drawList, Controls[RightTrigger], rt[0] >> 8);

	drawButton(drawList, Controls[AnalogArea], true);
	drawButton(drawList, Controls[AnalogStick], false);

	drawButton(drawList, Controls[FastForward], false);
	AlphaTrans += ((float)Visible - AlphaTrans) / 2;
}

static float getUIScale() {
	// scale is 1.1@ for a 320 dpi screen of height 750
	return 1.1f * 750.f / settings.display.height * settings.display.dpi / 320.f;
}

struct LayoutElement
{
	const std::string name;
	const float dx, dy;	// default pos in dc coords, relative to sides (or middle if 0)
	const float dw, dh;	// default size in dc coords

	float x, y;			// normalized coordinates [0, 1]
	float w, h;			// normalized coordinates [0, 1], scaled with uiScale
	float scale;		// user scale

	void load()
	{
		x = cfgLoadFloat(SECTION, name + "_x", x);
		y = cfgLoadFloat(SECTION, name + "_y", y);
		scale = cfgLoadFloat(SECTION, name + "_scale", scale);
	}
	void save() const
	{
		cfgSaveFloat(SECTION, name + "_x", x);
		cfgSaveFloat(SECTION, name + "_y", y);
		cfgSaveFloat(SECTION, name + "_scale", scale);
	}

	bool hitTest(float nx, float ny) const {
		return nx >= x && nx < x + w * scale
			&& ny >= y && ny < y + h * scale;
	}

	void applyUiScale()
	{
		const float dcw = 480.f * (float)settings.display.width / settings.display.height;
		const float uiscale = getUIScale();
		w = dw / dcw * uiscale;
		h = dh / 480.f * uiscale;
	}

	void reset()
	{
		scale = 1.f;
		const float dcw = 480.f * (float)settings.display.width / settings.display.height;
		const float uiscale = getUIScale();
		if (dx == 0)
			x = 0.5f - w / 2;
		else if (dx > 0)
			x = dx / dcw * uiscale;
		else
			x = 1.f - w + dx / dcw * uiscale;
		if (dy == 0)
			y = 0.5f - h / 2;
		else if (dy > 0)
			y = dy / 480.f * uiscale;
		else
			y = 1.f - h + dy / 480.f * uiscale;
	}

	static constexpr char const *SECTION = "vgamepad";
};
static LayoutElement Layout[] {
	{ "dpad",      32.f, -24.f, 192.f, 192.f },
	{ "buttons",  -24.f, -24.f, 192.f, 192.f },
	{ "start",      0.f, -24.f,  64.f,  64.f },
	{ "LT",      -134.f,-240.f,  90.f,  64.f },
	{ "RT",       -32.f,-240.f,  90.f,  64.f },
	{ "analog",    40.f,-320.f, 128.f, 128.f },
	{ "fforward", -24.f,  24.f,  64.f,  64.f },
};

static void applyLayout()
{
	const float dcw = 480.f * (float)settings.display.width / settings.display.height;
	const float dx = (dcw - 640.f) / 2;
	const float uiscale = getUIScale();
	float x, y, scale;

	// DPad
	x = Layout[Elem_DPad].x * dcw - dx;
	y = Layout[Elem_DPad].y * 480.f;
	scale = Layout[Elem_DPad].scale * uiscale;
	Controls[Left].pos =   { x + 0.f * scale, y + 64.f * scale };
	Controls[Up].pos =     { x + 64.f * scale, y + 0.f * scale };
	Controls[Right].pos =  { x + 128.f * scale, y + 64.f * scale };
	Controls[Down].pos =   { x + 64.f * scale, y + 128.f * scale };
	for (int control = Left; control <= Down; control++)
		Controls[control].size = { 64.f * scale, 64.f * scale };

	Controls[LeftUp].pos =     { x + 0.f * scale, y + 0.f * scale };
	Controls[LeftDown].pos =   { x + 0.f * scale, y + 128.f * scale };
	Controls[RightUp].pos =    { x + 128.f * scale, y + 0.f * scale };
	Controls[RightDown].pos =  { x + 128.f * scale, y + 128.f * scale };
	for (int control = LeftUp; control <= RightDown; control++)
		Controls[control].size = { 64.f * scale, 64.f * scale };

	// Buttons
	x = Layout[Elem_Buttons].x * dcw - dx;
	y = Layout[Elem_Buttons].y * 480.f;
	scale = Layout[Elem_Buttons].scale * uiscale;
	Controls[X].pos =  { x + 0.f * scale, y + 64.f * scale };
	Controls[Y].pos =  { x + 64.f * scale, y + 0.f * scale };
	Controls[B].pos =  { x + 128.f * scale, y + 64.f * scale };
	Controls[A].pos =  { x + 64.f * scale, y + 128.f * scale };
	for (int control = X; control <= A; control++)
		Controls[control].size = { 64.f * scale, 64.f * scale };

	// Start
	scale = Layout[Elem_Start].scale * uiscale;
	Controls[Start].pos =   { Layout[Elem_Start].x * dcw - dx, Layout[Elem_Start].y * 480.f };
	Controls[Start].size =  { Layout[Elem_Start].dw * scale, Layout[Elem_Start].dh * scale };

	// Left trigger
	scale = Layout[Elem_LT].scale * uiscale;
	Controls[LeftTrigger].pos = { Layout[Elem_LT].x * dcw - dx, Layout[Elem_LT].y * 480.f };
	Controls[LeftTrigger].size = { Layout[Elem_LT].dw * scale, Layout[Elem_LT].dh * scale };

	// Right trigger
	scale = Layout[Elem_RT].scale * uiscale;
	Controls[RightTrigger].pos =  { Layout[Elem_RT].x * dcw - dx, Layout[Elem_RT].y * 480.f };
	Controls[RightTrigger].size = { Layout[Elem_RT].dw * scale, Layout[Elem_RT].dh * scale };

	// Analog
	x = Layout[Elem_Analog].x * dcw - dx;
	y = Layout[Elem_Analog].y * 480.f;
	scale = Layout[Elem_Analog].scale * uiscale;
	Controls[AnalogArea].pos =   { x, y };
	Controls[AnalogArea].size =  { Layout[Elem_Analog].dw * scale, Layout[Elem_Analog].dh * scale };
	Controls[AnalogStick].pos =  { x + 32.f * scale, y + 32.f * scale };
	Controls[AnalogStick].size = { 64.f * scale, 64.f * scale };

	// Fast forward
	scale = Layout[Elem_FForward].scale * uiscale;
	Controls[FastForward].pos =  { Layout[Elem_FForward].x * dcw - dx, Layout[Elem_FForward].y * 480.f };
	Controls[FastForward].size = { Layout[Elem_FForward].dw * scale, Layout[Elem_FForward].dh * scale };
}

void applyUiScale() {
	for (auto& element : Layout)
		element.applyUiScale();
}

static void loadLayout()
{
	for (auto& element : Layout) {
		element.reset();
		element.load();
	}
	applyLayout();
}

static void saveLayout()
{
	cfgSetAutoSave(false);
	for (auto& element : Layout)
		element.save();
	cfgSetAutoSave(false);
}

static void resetLayout()
{
	for (auto& element : Layout)
		element.reset();
	applyLayout();
}

Element layoutHitTest(float x, float y)
{
	for (const auto& element : Layout)
		if (element.hitTest(x, y))
			return static_cast<Element>(&element - &Layout[0]);
	return Elem_None;
}

void translateElement(Element element, float dx, float dy)
{
	LayoutElement& e = Layout[element];
	e.x += dx;
	e.y += dy;
	applyLayout();
}

void scaleElement(Element element, float factor)
{
	LayoutElement& e = Layout[element];
	float dx = e.w * e.scale * (factor - 1.f) / 2.f;
	float dy = e.h * e.scale * (factor - 1.f) / 2.f;
	e.scale *= factor;
	// keep centered
	translateElement(element, -dx, -dy);
}

#ifndef __ANDROID__

void startEditing() {
	show();
}

void pauseEditing() {
}

void stopEditing(bool canceled)
{
	if (canceled)
		loadLayout();
	else
		saveLayout();
}

void resetEditing() {
	resetLayout();
}

#endif
}	// namespace vgamepad

#endif // __ANDROID__
