#pragma once

#include "SmartDetail.hpp"

#include <cstddef>
#include <vector>

class GameObject;
class PlayLayer;

namespace zaidultra {

struct DetailRendererStats {
    std::size_t highDetailObjects = 0;
    std::size_t decorationObjects = 0;
    std::size_t hiddenObjects = 0;
};

class SmartDetailRenderer final {
public:
    static SmartDetailRenderer& get();

    void reset();
    void scan(PlayLayer* layer);
    void apply(PlayLayer* layer, DetailTier tier);

    [[nodiscard]] DetailRendererStats stats() const;

private:
    void restore(PlayLayer* layer);
    void hideObjects(std::vector<GameObject*> const& objects);

    std::vector<GameObject*> m_highDetail;
    std::vector<GameObject*> m_decoration;
    std::vector<GameObject*> m_hidden;
    DetailTier m_lastTier = DetailTier::Normal;
    bool m_scanned = false;
};

} // namespace zaidultra
