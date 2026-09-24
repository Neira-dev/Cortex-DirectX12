/*
* テンプレート
* C/C++を使用したWindowsアプリケーションの基本的なテンプレート
* ISO C++20 準拠
* Application/main.cpp
* 最終更新日: 2026/08/16
*/

#include "Window.h"
#include "DirectX12.h"
#include "Helper.h"
#include "llamaServer.h"
#include "Worker.h"
#include "GUI.h"
#include "imgui.h"
#include "RmlUiGUI.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ID.h>
#include <mutex>
#include "../AssetPackCore/GlobalPak.h"
#include "AssetPackKey.h"

namespace
{
	// RmlUi 側の Click me クリックを、ImGui 側の確認用カウンタへ反映するための簡易リスナー。
	// (RmlUi の入力判定が実際に効いているか、ImGui 側の数値で目視確認できるようにする)
	class ClickCounterListener : public Rml::EventListener
	{
	public:
		explicit ClickCounterListener(int* counter) : m_counter(counter) {}
		void ProcessEvent(Rml::Event&) override
		{
			if (m_counter)
			{
				++(*m_counter);
			}
		}
	private:
		int* m_counter;
	};
}

int TemplateMain(HINSTANCE hInstance, int nCmdShow)
{
	Window* window = Window::GetInstance();
	DirectX::DirectX12* directX12 = DirectX::DirectX12::Get();
	DirectX::Descriptor* descriptor = DirectX::Descriptor::Get();

	// RmlUi の Click me と ImGui の Click me、両方のクリックをここへ集約する
	int clickCounter = 0;
	ClickCounterListener clickCounterListener(&clickCounter);

	/* 初期化 */
	{
		/* アセットパック (.cpak) を開く — 配布ビルド用の Assets.cpak がリポジトリルート
		   (作業ディレクトリ) に存在すればそれ以降 Mesh::Load / Texture::LoadInternal は
		   自動的にそちらを優先する。ファイルが無い場合は何もせず false のまま残り、
		   開発中と同じくルーズファイル読み込みにフォールバックし続けるので安全に常時呼べる。
		   鍵は AssetPackCore::DefaultKey() の既定引数には依存せず、AssetPackKey.h の
		   GetProjectPakKey() を明示的に渡す (AssetPacker --key-file と同じ鍵にすること)。 */
		const AssetPack::Key32& pakKey = AppAssetPack::GetProjectPakKey();
#ifndef _DEBUG
		// Release ビルドで AssetPackKey.h の鍵が未カスタマイズ (ライブラリの公開既定鍵の
		// まま) だと、Assets.cpak を暗号化しても意味が無い (鍵がソースに公開されているため)。
		// 気づかずに出荷することを防ぐため、Release では明示的に警告する。
		if (pakKey == AssetPack::DefaultKey())
		{
			MessageBox(nullptr,
				"AssetPackKey.h の鍵が未カスタマイズです (AssetPackCore::DefaultKey() のまま)。\n"
				"このまま配布すると Assets.cpak の暗号化キーが公開ソースと同じになり、保護の意味がありません。\n"
				"Application/AssetPackKey.h の GetProjectPakKey() を専用の鍵に差し替えてください。",
				"AssetPack Warning", MB_OK | MB_ICONWARNING);
		}
#endif
		AssetPack::GlobalPak::Get()->TryOpen("Assets.cpak", pakKey);

		/* Windowの初期化 */
		window->SetWindowTitle("Template");
		window->SetUseCustomTitleBar(false);
		window->SetWindowWidth(720);
		window->SetWindowHeight(480);
		// タイトルバーの方式を切り替えるフラグ (Initialize より前に呼ぶ)
		//   true  : 自作カスタムタイトルバー / false : OS標準タイトルバー
		window->Initialize(hInstance, nCmdShow);

		HWND Sub = window->CreateSubSeparateWindow(1, "Sub Window", 1000, 100, 400, 900);
		window->CreateTextBox(2, "", 5, 10, 370, 700, Sub);
		window->CreateRichEdit(3, "", 5, 720, 300, 100, Sub);
		window->CreateButton(4, "Send", 310, 720, 60, 100, Sub);

		/* DirectX12の初期化 */
		directX12->Init(window->GetMainWindowHandle(), window->GetClientWidth(), window->GetClientHeight());

		// ウィンドウのリサイズを DirectX12 へ伝える。これが無いとスワップチェーンが初期サイズ
		// のまま固定され、ImGui の io.DisplaySize (実クライアントサイズ) と食い違って
		// マウス判定位置がズレる。実際の再構築は毎フレームの ApplyResizeIfNeeded() が行う。
		window->SetResizeCallback([directX12](int w, int h)
			{
				directX12->RequestResize(static_cast<UINT>(w), static_cast<UINT>(h));
				RmlUiGUI::Get()->Resize(static_cast<UINT>(w), static_cast<UINT>(h));
			});

		/* Descriptorの初期化 */
		descriptor->Init();

		/* ImGui (GUI) の初期化 — DirectX12 / Descriptor の後 */
		GUI::Get()->Init(window->GetMainWindowHandle());

		/* RmlUi (実ゲーム UI) の初期化 — DirectX12 / Descriptor の後 */
		RmlUiGUI::Get()->Init(window->GetMainWindowHandle(), window->GetClientWidth(), window->GetClientHeight());
		if (Rml::ElementDocument* demoDoc = RmlUiGUI::Get()->LoadDocument("Assets/UI/demo.rml"))
		{
			// RmlUi 側の Click me クリックが実際に判定されているか、ImGui のカウンタで確認できるようにする
			if (Rml::Element* demoButton = demoDoc->GetElementById("demo-button"))
			{
				demoButton->AddEventListener(Rml::EventId::Click, &clickCounterListener);
			}
		}
	}

	/* ループ処理 */
	{
		std::mutex llmResultMutex;
		std::string m_llmResult = "";
		size_t displayedLength = 0; // m_llmResultのうち、既に画面に表示済みのバイト数
		Worker* worker = nullptr;
		bool aiPrefixShown = false;  // "AI: "を表示済みか
		bool resultDisplayed = true; // 末尾の改行を表示済みか
		LocalLLM::StreamStats lastStats; // 直近の生成速度(完了後にのみ読む。worker->IsFinished()のatomic同期に相乗りするのでmutex不要)

		/* 基礎ループ */
		MSG msg = {};

		bool isRunning = false;

		try
		{
			while (true)
			{
				// 溜まっているメッセージを毎フレーム すべて 捌いてから 1 フレーム描画する。
				// (if/else で「メッセージ処理 か 描画 か」を交互にすると、マウス操作中は
				//  メッセージが連続で来て描画フレームがほぼ回らず、ImGui の入力が死ぬ)
				bool quit = false;
				while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
				{
					if (msg.message == WM_QUIT)
					{
						quit = true;
						break;
					}
					TranslateMessage(&msg);
					DispatchMessageA(&msg);
				}
				if (quit)
				{
					break;
				}

				{
					// 更新処理

					// ImGui フレーム開始 — この後に UI を構築する
					GUI::Get()->BeginFrame();

					// RmlUi 側の更新 (アニメーション・データバインディング等)
					RmlUiGUI::Get()->Update();

					// --- 動作確認用の小さなテストウィンドウ (720x480 でも収まる位置・サイズ) ---
					{
						ImGui::SetNextWindowPos(ImVec2(380, 280), ImGuiCond_FirstUseEver);
						ImGui::SetNextWindowSize(ImVec2(320, 170), ImGuiCond_FirstUseEver);
						ImGui::Begin("GUI Test");
						ImGui::Text("クリック確認 (RmlUi 側のクリックもここに加算されます)");
						if (ImGui::Button("Click me"))
						{
							clickCounter++;
						}
						ImGui::SameLine();
						ImGui::Text("count = %d", clickCounter);
						static bool checked = false;
						ImGui::Checkbox("checkbox", &checked);
						{
							const ImGuiIO& io = ImGui::GetIO();
							ImGui::Separator();
							ImGui::Text("mouse   : (%.0f, %.0f)  capture=%d", io.MousePos.x, io.MousePos.y, (int)io.WantCaptureMouse);
							ImGui::Text("Display : %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
							ImGui::Text("Client  : %d x %d", window->GetClientWidth(), window->GetClientHeight());
							// io.DisplaySize と Client が一致していないと判定位置がズレる
						}
						ImGui::End();
					}
					// ImGui::ShowDemoWindow(); // フルデモ (720x480 だと画面外へはみ出す)

					if (window->IsButtonClicked(4) && (worker == nullptr || worker->IsFinished()))
					{
						window->AddTextBoxText(2, "You: " + window->GetEditText(3) + "\r\n\r\n");

						delete worker; // 前回分の後始末(joinable済みなのですぐ壊せる)

						std::string ansiInput = window->GetEditText(3);
						std::string utf8Input = Helper::AnsiToUtf8(ansiInput);
						window->SetBoxText(3, ""); // 入力欄をクリア

						{
							std::lock_guard<std::mutex> lock(llmResultMutex);
							m_llmResult.clear();
						}
						displayedLength = 0;
						aiPrefixShown = false;
						resultDisplayed = false;

						worker = new Worker([&m_llmResult, &llmResultMutex, &lastStats, utf8Input]()
							{
								LocalLLM::CallLlamaServerStream(utf8Input, [&m_llmResult, &llmResultMutex](const std::string& delta)
									{
										std::lock_guard<std::mutex> lock(llmResultMutex);
										m_llmResult += delta;
									}, &lastStats);
							});
					}

					// 毎フレーム: 前回チェック時から新しく届いた分だけ切り出して表示する
					if (worker)
					{
						std::string newUtf8;
						{
							std::lock_guard<std::mutex> lock(llmResultMutex);
							if (m_llmResult.size() > displayedLength)
							{
								newUtf8 = m_llmResult.substr(displayedLength);
								displayedLength = m_llmResult.size();
							}
						}

						if (!newUtf8.empty())
						{
							if (!aiPrefixShown)
							{
								window->AddTextBoxText(2, "AI: ");
								aiPrefixShown = true;
							}
							window->AddTextBoxText(2, Helper::Utf8ToAnsi(newUtf8));
						}

						if (worker->IsFinished() && !resultDisplayed)
						{
							char speedText[64];
							sprintf_s(speedText, "(%.1f tok/s)", lastStats.tokensPerSecond);
							window->AddTextBoxText(2, std::string(speedText) + "\r\n\r\n");
							resultDisplayed = true;
						}
					}



					// 描画処理
					directX12->BeginDraw();

					// ここに描画処理を追加する

					// RmlUi (実ゲーム UI) をシーン描画の後、ImGui (デバッグツール) より先に描画
					RmlUiGUI::Get()->Render(directX12->GetCommandList(DirectX::DirectX12::CmdListType::Direct));

					// シーン描画をすべて積んだ後、ImGui をバックバッファへ描画
					GUI::Get()->EndFrame(directX12->GetCommandList(DirectX::DirectX12::CmdListType::Direct));

					directX12->EndDraw();

					// ドッキング/マルチビューポートのサブウィンドウ (EndDraw の後)
					GUI::Get()->RenderMultiViewport();

					directX12->ApplyResizeIfNeeded();

				}
			}
		}
		catch (const std::exception& e)
		{
			MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
		}
		catch (...)
		{
			MessageBoxA(nullptr, "Unknown error occurred.", "Error", MB_OK | MB_ICONERROR);
		}
	}

	/* 終了処理 */
	{
		RmlUiGUI::Del();     // RmlUi / ImGui のバックエンドを先に破棄 (device / descriptor がまだ生きている必要がある)
		GUI::Del();
		descriptor->Del();
		directX12->Del();
		window->DestroyInstance();
	}

	return 0;
}