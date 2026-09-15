#include "RmlUiGUI.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger/Debugger.h>
#include <windowsx.h>

RmlUiGUI* RmlUiGUI::s_instance = nullptr;

namespace
{
	int GetKeyModifierState()
	{
		int mod = 0;
		if (::GetKeyState(VK_CONTROL) & 0x8000) mod |= Rml::Input::KM_CTRL;
		if (::GetKeyState(VK_SHIFT) & 0x8000)   mod |= Rml::Input::KM_SHIFT;
		if (::GetKeyState(VK_MENU) & 0x8000)    mod |= Rml::Input::KM_ALT;
		if (::GetKeyState(VK_CAPITAL) & 0x0001) mod |= Rml::Input::KM_CAPSLOCK;
		if (::GetKeyState(VK_NUMLOCK) & 0x0001) mod |= Rml::Input::KM_NUMLOCK;
		return mod;
	}

	// 主要キーだけの簡易対応表 (テキスト入力・ナビゲーションに十分な範囲)
	Rml::Input::KeyIdentifier VirtualKeyToRmlKey(WPARAM vk)
	{
		using namespace Rml::Input;
		if (vk >= '0' && vk <= '9') return static_cast<KeyIdentifier>(KI_0 + (vk - '0'));
		if (vk >= 'A' && vk <= 'Z') return static_cast<KeyIdentifier>(KI_A + (vk - 'A'));
		if (vk >= VK_F1 && vk <= VK_F12) return static_cast<KeyIdentifier>(KI_F1 + (vk - VK_F1));
		switch (vk)
		{
		case VK_BACK:    return KI_BACK;
		case VK_TAB:     return KI_TAB;
		case VK_RETURN:  return KI_RETURN;
		case VK_ESCAPE:  return KI_ESCAPE;
		case VK_SPACE:   return KI_SPACE;
		case VK_PRIOR:   return KI_PRIOR;
		case VK_NEXT:    return KI_NEXT;
		case VK_END:     return KI_END;
		case VK_HOME:    return KI_HOME;
		case VK_LEFT:    return KI_LEFT;
		case VK_UP:      return KI_UP;
		case VK_RIGHT:   return KI_RIGHT;
		case VK_DOWN:    return KI_DOWN;
		case VK_INSERT:  return KI_INSERT;
		case VK_DELETE:  return KI_DELETE;
		case VK_SHIFT:   return KI_LSHIFT;
		case VK_CONTROL: return KI_LCONTROL;
		case VK_MENU:    return KI_LMENU;
		default:         return KI_UNKNOWN;
		}
	}
}

RmlUiGUI* RmlUiGUI::Get()
{
	if (s_instance == nullptr)
	{
		s_instance = new RmlUiGUI();
	}
	return s_instance;
}

void RmlUiGUI::Del()
{
	if (s_instance != nullptr)
	{
		s_instance->Shutdown();
		delete s_instance;
		s_instance = nullptr;
	}
}

bool RmlUiGUI::Init(HWND hwnd, UINT width, UINT height)
{
	if (m_initialized)
	{
		return true;
	}

	if (!m_renderInterface.Init())
	{
		return false;
	}

	Rml::SetSystemInterface(&m_systemInterface);
	Rml::SetRenderInterface(&m_renderInterface);

	if (!Rml::Initialise())
	{
		m_renderInterface.Shutdown();
		return false;
	}

	// v1: 専用フォントを同梱していないので、OS 標準フォントを暫定で使う。
	// 注意: Segoe UI は日本語グリフを持たない。fallback_face で他書体を追加しても
	// (Yu Gothic 等で検証済み) 期待通りにフォールバックされないケースを確認しており、
	// 日本語を含むテキストは現状レイアウトが崩れる可能性がある (要調査、TODO)。
	Rml::LoadFontFace("C:/Windows/Fonts/segoeui.ttf", true);

	m_context = Rml::CreateContext("main", Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
	if (!m_context)
	{
		Rml::Shutdown();
		m_renderInterface.Shutdown();
		return false;
	}

	// 公式 Debugger プラグイン。既定は非表示 (F8 でトグル、WndProc 経由 ToggleDebugger())。
	// .rml/.rcss を手書きする代わりに、実行中のツリー構造・適用中のプロパティをその場で確認できる
	Rml::Debugger::Initialise(m_context);

	m_initialized = true;
	return true;
}

void RmlUiGUI::Shutdown()
{
	if (!m_initialized)
	{
		return;
	}
	Rml::Shutdown(); // "main" コンテキストも含めて破棄される
	m_context = nullptr;
	m_renderInterface.Shutdown();
	m_initialized = false;
}

void RmlUiGUI::Resize(UINT width, UINT height)
{
	if (m_context)
	{
		m_context->SetDimensions(Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
	}
}

void RmlUiGUI::Update()
{
	if (m_context)
	{
		m_context->Update();
	}
}

void RmlUiGUI::Render(ID3D12GraphicsCommandList* commandList)
{
	if (!m_initialized || !m_context)
	{
		return;
	}
	const Rml::Vector2i dims = m_context->GetDimensions();
	m_renderInterface.BeginFrame(commandList, static_cast<UINT>(dims.x), static_cast<UINT>(dims.y));
	m_context->Render();
}

void RmlUiGUI::ToggleDebugger()
{
	Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
}

Rml::ElementDocument* RmlUiGUI::LoadDocument(const std::string& path)
{
	if (!m_context)
	{
		return nullptr;
	}
	Rml::ElementDocument* doc = m_context->LoadDocument(path);
	if (doc)
	{
		doc->Show();
	}
	return doc;
}

bool RmlUiGUI::ProcessWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!m_initialized || !m_context)
	{
		return false;
	}

	const int mod = GetKeyModifierState();

	// Context::Process* は「true = RmlUi が消費せず呼び出し元へ伝播してよい」を返すので、
	// ここでは反転して「true = RmlUi が消費した」に統一する
	switch (message)
	{
	case WM_MOUSEMOVE:
		return !m_context->ProcessMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), mod);

	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	{
		const int button = (message == WM_LBUTTONDOWN) ? 0 : (message == WM_RBUTTONDOWN) ? 1 : 2;
		if (m_mouseButtonsDown == 0)
		{
			::SetCapture(hwnd);
		}
		m_mouseButtonsDown |= (1 << button);
		return !m_context->ProcessMouseButtonDown(button, mod);
	}

	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	{
		const int button = (message == WM_LBUTTONUP) ? 0 : (message == WM_RBUTTONUP) ? 1 : 2;
		m_mouseButtonsDown &= ~(1 << button);
		const bool consumed = !m_context->ProcessMouseButtonUp(button, mod);
		if (m_mouseButtonsDown == 0)
		{
			::ReleaseCapture();
		}
		return consumed;
	}

	case WM_MOUSEWHEEL:
		return !m_context->ProcessMouseWheel(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA, mod);

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		if (wParam == VK_F8)
		{
			ToggleDebugger();
			return true;
		}
		const Rml::Input::KeyIdentifier key = VirtualKeyToRmlKey(wParam);
		if (key == Rml::Input::KI_UNKNOWN)
		{
			return false;
		}
		return !m_context->ProcessKeyDown(key, mod);
	}

	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		const Rml::Input::KeyIdentifier key = VirtualKeyToRmlKey(wParam);
		if (key == Rml::Input::KI_UNKNOWN)
		{
			return false;
		}
		return !m_context->ProcessKeyUp(key, mod);
	}

	case WM_CHAR:
		// v1: 制御文字を除く ASCII のみ (IME 経由の全角文字等は未対応)
		if (wParam >= 32 && wParam < 127)
		{
			return !m_context->ProcessTextInput(static_cast<char>(wParam));
		}
		return false;

	default:
		return false;
	}
}

// Windows.vcxproj の WndProc (Window.cpp) から extern 参照される。
// RmlUiGUI.lib は最終的な Application リンク時に解決される (ImGui_ImplWin32_WndProcHandler と同じ仕組み)。
bool RmlUiGUI_ProcessWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return RmlUiGUI::Get()->ProcessWin32Message(hwnd, message, wParam, lParam);
}
