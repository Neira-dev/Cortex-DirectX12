#pragma once
#include "RenderInterface_DX12.h"
#include "SystemInterface_Cortex.h"
#include <RmlUi/Core/Context.h>
#include <Windows.h>
#include <d3d12.h>
#include <string>

/*
* RmlUiGUI
* Rml::Core の Cortex 向けラッパ。シングルトン (GUI/ImGui と同じ位置付け)。
*
* 呼び出し順:
*   DirectX12::Init() -> Descriptor::Init() -> RmlUiGUI::Get()->Init(hwnd, w, h)
*   毎フレーム: Update() -> DirectX12::BeginDraw() -> (シーン描画) -> Render(cmdList) -> DirectX12::EndDraw()
*   WndProc:    ImGui が捕捉しなかったメッセージだけ ProcessWin32Message() へ回す
*   終了時:     RmlUiGUI::Del()  (DirectX12::Del() より前)
*
* ImGui との使い分け: RmlUiGUI = 実ゲーム UI (.rml/.rcss)、GUI(ImGui) = デバッグツール。
*/
class RmlUiGUI
{
public:
	static RmlUiGUI* Get();
	static void Del();

	/**
	 * @brief RmlUi を初期化し、"main" という名前のコンテキストを1つ作成する
	 * @note DirectX12 と Descriptor の Init 後に呼ぶこと
	 */
	bool Init(HWND hwnd, UINT width, UINT height);
	void Shutdown();

	/**
	 * @brief ウィンドウリサイズをコンテキストへ伝える
	 */
	void Resize(UINT width, UINT height);

	/**
	 * @brief アニメーション・データバインディング等を更新する (BeginDraw より前)
	 */
	void Update();

	/**
	 * @brief 蓄積された描画コマンドを cmd へ積む
	 * @param commandList DirectX12::BeginDraw 済みの Direct コマンドリスト
	 * @note シーン描画の後、DirectX12::EndDraw の前に呼ぶこと
	 */
	void Render(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief Win32 メッセージを RmlUi の入力として転送する
	 * @return RmlUi がこのメッセージを消費した(=呼び出し側でこれ以上処理不要)なら true
	 * @note ImGui が io.WantCaptureMouse/WantCaptureKeyboard な間は呼ばないこと (入力の奪い合い回避)
	 */
	bool ProcessWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	/**
	 * @brief "main" コンテキストへ .rml を読み込む
	 */
	Rml::ElementDocument* LoadDocument(const std::string& path);

	/**
	 * @brief RmlUi 公式の要素インスペクタ (Debugger プラグイン) の表示/非表示を切り替える
	 * @note WndProc から F8 キーで呼ばれる。ツリー構造・適用中の RCSS プロパティをその場で確認できる
	 */
	void ToggleDebugger();

	Rml::Context* GetContext() const { return m_context; }
	bool IsInitialized() const { return m_initialized; }

private:
	RmlUiGUI() = default;
	~RmlUiGUI() = default;

private:
	RenderInterface_DX12   m_renderInterface;
	SystemInterface_Cortex m_systemInterface;
	Rml::Context* m_context = nullptr;
	bool m_initialized = false;
	int  m_mouseButtonsDown = 0; // SetCapture/ReleaseCapture の判定用ビットマスク

	static RmlUiGUI* s_instance;
};
