// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#include "pch.h"


#ifndef WORLD_EXPORTS
#define WORLD_EXPORTS
#endif

#ifdef WORLD_EXPORTS
#define WORLD_API __declspec(dllexport)
#else
#define WORLD_API __declspec(dllimport)
#endif

#include <vector>
#include <memory>
#include <cmath>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <cstdint>

namespace World {

    struct WORLD_API Point2D {
        int x, y;
        bool operator==(const Point2D& other) const { return x == other.x && y == other.y; }
        bool operator!=(const Point2D& other) const { return !(*this == other); }
    };

    struct WORLD_API Rect {
        int x, y, width, height;
        bool Contains(const Point2D& p) const {
            return (p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height);
        }
        bool Intersects(const Rect& other) const {
            return !(other.x > x + width || other.x + other.width < x ||
                other.y > y + height || other.y + other.height < y);
        }
    };

    class WORLD_API IsometricCoordinateSystem {
    public:
        static constexpr int TILE_WIDTH = 48;
        static constexpr int TILE_HEIGHT = 24;

        static Point2D MapToWorld(int mapX, int mapY) {
            Point2D world;
            world.x = (mapX - mapY) * (TILE_WIDTH / 2);
            world.y = (mapX + mapY) * (TILE_HEIGHT / 2);
            return world;
        }

        static Point2D WorldToMap(int worldX, int worldY) {
            Point2D map;
            float halfWidth = TILE_WIDTH / 2.0f;
            float halfHeight = TILE_HEIGHT / 2.0f;

            map.x = static_cast<int>(std::round((worldX / halfWidth + worldY / halfHeight) / 2.0f));
            map.y = static_cast<int>(std::round((worldY / halfHeight - worldX / halfWidth) / 2.0f));
            return map;
        }
    };

    enum class ObjectType {
        PLAYER,
        MONSTER,
        NPC,
        ITEM_DROP,
        PORTAL,
        SCENE_DECOR
    };

    class WORLD_API WorldObject {
    public:
        virtual ~WorldObject() = default;

        uint32_t id;
        ObjectType type;
        Point2D mapPosition;

        virtual Rect GetBounds() const {
            Point2D wp = IsometricCoordinateSystem::MapToWorld(mapPosition.x, mapPosition.y);
            return Rect{ wp.x - 24, wp.y - 24, 48, 48 };
        }
    };

    class WORLD_API Portal : public WorldObject {
    public:
        Portal() { type = ObjectType::PORTAL; }
        uint32_t targetMapId;
        Point2D targetPosition;
    };

    struct WORLD_API LogicalTile {
        int16_t elevation;
        bool isWalkable;
        bool isShootThrough;
    };

    class WORLD_API LogicalMap {
    public:
        void Initialize(int width, int height) {
            m_width = width;
            m_height = height;
            m_tiles.resize(m_width * m_height);
        }

        void SetTile(int x, int y, int16_t elevation, bool walkable, bool shootThrough = true) {
            if (IsValid(x, y)) {
                auto& tile = m_tiles[y * m_width + x];
                tile.elevation = elevation;
                tile.isWalkable = walkable;
                tile.isShootThrough = shootThrough;
            }
        }

        bool IsValid(int x, int y) const {
            return x >= 0 && x < m_width && y >= 0 && y < m_height;
        }

        bool IsWalkable(int x, int y) const {
            if (!IsValid(x, y)) return false;
            return m_tiles[y * m_width + x].isWalkable;
        }

        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

    private:
        int m_width = 0;
        int m_height = 0;
        std::vector<LogicalTile> m_tiles;
    };

    class WORLD_API QuadTree {
    public:
        QuadTree(const Rect& boundary, int capacity)
            : m_boundary(boundary), m_capacity(capacity), m_divided(false) {
        }

        bool Insert(std::shared_ptr<WorldObject> obj) {
            if (!m_boundary.Intersects(obj->GetBounds())) return false;

            if (m_objects.size() < m_capacity && !m_divided) {
                m_objects.push_back(obj);
                return true;
            }

            if (!m_divided) Subdivide();

            if (nw->Insert(obj)) return true;
            if (ne->Insert(obj)) return true;
            if (sw->Insert(obj)) return true;
            if (se->Insert(obj)) return true;

            return false;
        }

        void Query(const Rect& range, std::vector<std::shared_ptr<WorldObject>>& found) const {
            if (!m_boundary.Intersects(range)) return;

            for (const auto& obj : m_objects) {
                if (range.Intersects(obj->GetBounds())) {
                    found.push_back(obj);
                }
            }

            if (m_divided) {
                nw->Query(range, found);
                ne->Query(range, found);
                sw->Query(range, found);
                se->Query(range, found);
            }
        }

        void Clear() {
            m_objects.clear();
            m_divided = false;
            nw.reset(); ne.reset(); sw.reset(); se.reset();
        }

    private:
        void Subdivide() {
            int x = m_boundary.x;
            int y = m_boundary.y;
            int w = m_boundary.width / 2;
            int h = m_boundary.height / 2;

            nw = std::make_unique<QuadTree>(Rect{ x, y, w, h }, m_capacity);
            ne = std::make_unique<QuadTree>(Rect{ x + w, y, w, h }, m_capacity);
            sw = std::make_unique<QuadTree>(Rect{ x, y + h, w, h }, m_capacity);
            se = std::make_unique<QuadTree>(Rect{ x + w, y + h, w, h }, m_capacity);
            m_divided = true;
        }

        Rect m_boundary;
        size_t m_capacity;
        bool m_divided;
        std::vector<std::shared_ptr<WorldObject>> m_objects;

        std::unique_ptr<QuadTree> nw, ne, sw, se;
    };

    class WORLD_API AStarPathfinder {
    private:
        struct Node {
            Point2D pos;
            int gCost, hCost;
            Node* parent;

            int fCost() const { return gCost + hCost; }
            bool operator>(const Node& other) const { return fCost() > other.fCost(); }
        };

        int CalculateHeuristic(Point2D a, Point2D b) {
            return (std::max)(std::abs(a.x - b.x), std::abs(a.y - b.y)) * 10;
        }

    public:
        std::vector<Point2D> FindPath(const LogicalMap& map, Point2D start, Point2D target) {
            std::vector<Point2D> path;
            if (!map.IsWalkable(target.x, target.y) || !map.IsWalkable(start.x, start.y)) return path;

            auto compare = [](const Node* a, const Node* b) { return a->fCost() > b->fCost(); };
            std::priority_queue<Node*, std::vector<Node*>, decltype(compare)> openList(compare);

            std::unordered_map<int, bool> closedList;
            auto getHash = [&map](const Point2D& p) { return p.y * map.GetWidth() + p.x; };

            Node* startNode = new Node{ start, 0, CalculateHeuristic(start, target), nullptr };
            openList.push(startNode);

            std::vector<Node*> allNodes;
            allNodes.push_back(startNode);

            const int dirX[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            const int dirY[] = { -1, -1, 0, 1, 1, 1, 0, -1 };

            while (!openList.empty()) {
                Node* current = openList.top();
                openList.pop();

                if (current->pos == target) {
                    Node* tracer = current;
                    while (tracer != nullptr) {
                        path.push_back(tracer->pos);
                        tracer = tracer->parent;
                    }
                    std::reverse(path.begin(), path.end());
                    break;
                }

                closedList[getHash(current->pos)] = true;

                for (int i = 0; i < 8; ++i) {
                    Point2D neighborPos = { current->pos.x + dirX[i], current->pos.y + dirY[i] };

                    if (!map.IsWalkable(neighborPos.x, neighborPos.y) || closedList[getHash(neighborPos)]) {
                        continue;
                    }

                    int moveCost = (dirX[i] != 0 && dirY[i] != 0) ? 14 : 10;
                    int newGCost = current->gCost + moveCost;

                    Node* neighbor = new Node{ neighborPos, newGCost, CalculateHeuristic(neighborPos, target), current };
                    openList.push(neighbor);
                    allNodes.push_back(neighbor);
                }
            }

            for (Node* n : allNodes) delete n;
            return path;
        }
    };

    class WORLD_API WorldManager {
    public:
        void Initialize(int mapWidth, int mapHeight) {
            m_map.Initialize(mapWidth, mapHeight);

            Rect worldBounds = { -10000, -10000, 20000, 20000 };
            m_quadTree = std::make_unique<QuadTree>(worldBounds, 10);
        }

        void SetMapData(int x, int y, int16_t elevation, bool walkable) {
            m_map.SetTile(x, y, elevation, walkable);
        }

        void AddObject(std::shared_ptr<WorldObject> obj) {
            m_objects[obj->id] = obj;
            m_quadTree->Insert(obj);
        }

        void RemoveObject(uint32_t id) {
            m_objects.erase(id);
            RebuildQuadTree();
        }

        void Update(float deltaTime) {
        }

        std::vector<std::shared_ptr<WorldObject>> GetVisibleObjects(const Rect& cameraView) const {
            std::vector<std::shared_ptr<WorldObject>> visible;
            if (m_quadTree) {
                m_quadTree->Query(cameraView, visible);
            }
            return visible;
        }

        std::vector<Point2D> RequestPath(Point2D start, Point2D target) {
            return m_pathfinder.FindPath(m_map, start, target);
        }

    private:
        void RebuildQuadTree() {
            m_quadTree->Clear();
            for (const auto& pair : m_objects) {
                m_quadTree->Insert(pair.second);
            }
        }

        LogicalMap m_map;
        std::unordered_map<uint32_t, std::shared_ptr<WorldObject>> m_objects;
        std::unique_ptr<QuadTree> m_quadTree;
        AStarPathfinder m_pathfinder;
    };

}