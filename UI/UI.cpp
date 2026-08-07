// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#include "pch.h"

#ifndef UI_EXPORTS
#define UI_EXPORTS
#endif

#ifdef UI_EXPORTS
#define UI_API __declspec(dllexport)
#else
#define UI_API __declspec(dllimport)
#endif

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace UI {


    // Matemática simples de UI
    struct UI_API Rect {
        int x, y, width, height;
        bool Contains(int px, int py) const {
            return px >= x && px <= x + width && py >= y && py <= y + height;
        }
    };

    // Tipos de comandos que a UI pode pedir para a Graphics.dll desenhar
    enum class DrawCommandType {
        IMAGE,
        TEXT,
        RECTANGLE
    };

    // A instrução "burra" de desenho
    struct UI_API DrawCommand {
        DrawCommandType type;
        std::string resourceName; // Pode ser "GUI/Inventory.png" ou uma string de texto "Level Up!"
        Rect bounds;
        uint32_t colorRGBA;       // Para textos ou sobreposições
    };

    // ============================================================================
    // 1. BASE UI ELEMENTS
    // ============================================================================
    class UI_API UIElement {
    public:
        virtual ~UIElement() = default;

        std::string Name;
        Rect Bounds = { 0, 0, 0, 0 };
        bool IsVisible = true;
        bool IsDraggable = false;

        // Hierarquia
        UIElement* Parent = nullptr;
        std::vector<std::shared_ptr<UIElement>> Children;

        void AddChild(std::shared_ptr<UIElement> child) {
            child->Parent = this;
            Children.push_back(child);
        }

        // Gera as instruções visuais deste elemento e dos filhos
        virtual void BuildDrawCommands(std::vector<DrawCommand>& commandList) {
            if (!IsVisible) return;

            OnBuildDrawCommands(commandList); // Elemento atual se desenha

            for (auto& child : Children) {
                child->BuildDrawCommands(commandList); // Filhos se desenham por cima
            }
        }

        // Inputs lógicos (O Main.exe passa o mouse X,Y pra cá)
        virtual bool OnMouseClick(int mx, int my) {
            if (!IsVisible || !Bounds.Contains(mx, my)) return false;

            // Checa os filhos primeiro (Eles ficam na frente visualmente)
            for (auto it = Children.rbegin(); it != Children.rend(); ++it) {
                if ((*it)->OnMouseClick(mx, my)) return true;
            }
            return false; // Se eu não processei, retorna falso
        }

    protected:
        virtual void OnBuildDrawCommands(std::vector<DrawCommand>& commandList) {}
    };

    // Botão Lógico Genérico
    class UI_API UIButton : public UIElement {
    public:
        std::string TextureNormal;
        std::string TextureHover;
        std::string TexturePressed;
        std::function<void()> OnClickCallback;

        bool OnMouseClick(int mx, int my) override {
            if (UIElement::OnMouseClick(mx, my)) return true;

            if (Bounds.Contains(mx, my)) {
                if (OnClickCallback) OnClickCallback();
                return true; // Clique consumido!
            }
            return false;
        }

    protected:
        void OnBuildDrawCommands(std::vector<DrawCommand>& commandList) override {
            // Em um sistema real, você checaria estado de hover/pressed
            commandList.push_back({ DrawCommandType::IMAGE, TextureNormal, Bounds, 0xFFFFFFFF });
        }
    };

    // ============================================================================
    // 2. CONQUER SPECIFIC WINDOWS
    // ============================================================================
    class UI_API InventoryWindow : public UIElement {
    public:
        InventoryWindow() {
            Name = "Inventory";
            Bounds = { 500, 100, 250, 350 };
            IsDraggable = true;
            IsVisible = false; // Começa fechado
        }

        // Chamado pela Game.dll quando o servidor manda o pacote MsgItemInfo
        void UpdateItems(const std::vector<uint32_t>& itemIds) {
            m_items = itemIds;
            // Recria os "UIButtons" invisíveis para cada slot
        }

    protected:
        void OnBuildDrawCommands(std::vector<DrawCommand>& commandList) override {
            // Fundo do inventário
            commandList.push_back({ DrawCommandType::IMAGE, "GUI/Main/inventory_bg.png", Bounds, 0xFFFFFFFF });

            // Desenha os itens
            int slotX = Bounds.x + 10;
            int slotY = Bounds.y + 40;
            for (uint32_t itemId : m_items) {
                commandList.push_back({ DrawCommandType::IMAGE, "item_" + std::to_string(itemId) + ".png", {slotX, slotY, 32, 32}, 0xFFFFFFFF });
                slotX += 36;
                if (slotX > Bounds.x + 200) { slotX = Bounds.x + 10; slotY += 36; }
            }
        }
    private:
        std::vector<uint32_t> m_items;
    };

    class UI_API ChatWindow : public UIElement {
    public:
        ChatWindow() {
            Name = "Chat";
            Bounds = { 10, 450, 400, 140 }; // Canto inferior esquerdo clássico
        }

        void AddMessage(const std::string& sender, const std::string& text, uint32_t color) {
            m_messages.push_back({ sender, text, color });
            if (m_messages.size() > 50) m_messages.erase(m_messages.begin());
        }

    protected:
        void OnBuildDrawCommands(std::vector<DrawCommand>& commandList) override {
            commandList.push_back({ DrawCommandType::IMAGE, "GUI/Main/chat_bg.png", Bounds, 0x88000000 }); // Fundo semi-transparente

            int yPos = Bounds.y + Bounds.height - 20;
            // Desenha de baixo pra cima
            for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it) {
                if (yPos < Bounds.y) break;
                commandList.push_back({ DrawCommandType::TEXT, it->sender + ": " + it->text, {Bounds.x + 5, yPos, 0, 0}, it->color });
                yPos -= 18; // Altura da linha
            }
        }
    private:
        struct ChatMsg { std::string sender; std::string text; uint32_t color; };
        std::vector<ChatMsg> m_messages;
    };

    class UI_API NpcDialogWindow : public UIElement {
    public:
        NpcDialogWindow() {
            Name = "NpcDialog";
            Bounds = { 200, 150, 400, 200 };
            IsVisible = false;
        }

        void OpenDialog(uint32_t npcId, const std::string& text, const std::vector<std::string>& options) {
            IsVisible = true;
            m_text = text;
            // Na engine real, nós popularíamos UIButton dinamicamente no vetor Children para cada Option
        }

    protected:
        void OnBuildDrawCommands(std::vector<DrawCommand>& commandList) override {
            commandList.push_back({ DrawCommandType::IMAGE, "GUI/Main/npc_dialog.png", Bounds, 0xFFFFFFFF });
            commandList.push_back({ DrawCommandType::TEXT, m_text, {Bounds.x + 20, Bounds.y + 20, 0, 0}, 0xFF000000 });
        }
    private:
        std::string m_text;
    };

    // ============================================================================
    // 3. UI MANAGER (Orquestrador Global)
    // ============================================================================
    class UI_API UIManager {
    public:
        static UIManager& GetInstance() {
            static UIManager instance;
            return instance;
        }

        void Initialize() {
            // Instancia todas as janelas do jogo
            Inventory = std::make_shared<InventoryWindow>();
            Chat = std::make_shared<ChatWindow>();
            NpcDialog = std::make_shared<NpcDialogWindow>();
            // Minimap, Skills, Guild, etc...

            m_windows.push_back(Chat);
            m_windows.push_back(Inventory);
            m_windows.push_back(NpcDialog);
        }

        // Acesso direto para o Jogo conversar com a UI (A Game.dll chamará isso)
        std::shared_ptr<InventoryWindow> Inventory;
        std::shared_ptr<ChatWindow> Chat;
        std::shared_ptr<NpcDialogWindow> NpcDialog;

        // Retorna a lista final para o Client.exe passar para a Graphics.dll
        std::vector<DrawCommand> GenerateRenderCommands() {
            std::vector<DrawCommand> renderList;
            for (auto& win : m_windows) {
                win->BuildDrawCommands(renderList);
            }
            return renderList;
        }

        // Processamento de Input (Chamado pelo Client.exe)
        bool ProcessMouseClick(int x, int y) {
            // Itera de trás pra frente (para clicar na janela que está por cima - z-index)
            for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
                if ((*it)->OnMouseClick(x, y)) {
                    // Põe a janela em foco (Traz para o topo do vetor)
                    auto win = *it;
                    m_windows.erase(std::next(it).base());
                    m_windows.push_back(win);
                    return true; // O clique não passa pro World/Chão
                }
            }
            return false; // Clique foi no mundo 3D
        }

        void ProcessKeyDown(int keyCode) {
            // Exemplo de hotkey global
            if (keyCode == 'I') {
                Inventory->IsVisible = !Inventory->IsVisible;
            }
        }

    private:
        UIManager() = default;
        std::vector<std::shared_ptr<UIElement>> m_windows;
    };

} // namespace UI