#include "SmartDetailRenderer.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <unordered_set>

using namespace geode::prelude;

namespace zaidultra {

SmartDetailRenderer& SmartDetailRenderer::get() {
    static SmartDetailRenderer instance;
    return instance;
}

void SmartDetailRenderer::reset() {
    m_highDetail.clear();
    m_decoration.clear();
    m_hidden.clear();
    m_lastTier = DetailTier::Normal;
    m_scanned = false;
}

void SmartDetailRenderer::scan(PlayLayer* layer) {
    reset();
    if (!layer || !layer->m_objectLayer) {
        return;
    }

    std::unordered_set<GameObject*> seen;
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) {
            return;
        }

        if (auto object = typeinfo_cast<GameObject*>(node)) {
            if (seen.insert(object).second) {
                if (object->m_isHighDetail) {
                    m_highDetail.push_back(object);
                }
                if (object->m_objectType == GameObjectType::Decoration) {
                    m_decoration.push_back(object);
                }
            }
            // GameObject children are internal sprites/effects; they do not
            // need another traversal for the level-object cache.
            return;
        }

        auto children = node->getChildren();
        if (!children) {
            return;
        }
        for (auto child : CCArrayExt<CCNode*>(children)) {
            walk(child);
        }
    };

    walk(layer->m_objectLayer);
    m_scanned = true;
}

void SmartDetailRenderer::hideObjects(std::vector<GameObject*> const& objects) {
    for (auto object : objects) {
        if (!object) {
            continue;
        }
        object->setVisible(false);
        m_hidden.push_back(object);
    }
}

void SmartDetailRenderer::restore(PlayLayer* layer) {
    std::unordered_set<GameObject*> restored;
    for (auto object : m_hidden) {
        if (!object || !restored.insert(object).second) {
            continue;
        }
        // Visibility is immediately recalculated by updateVisibility below,
        // so trigger/camera state remains authoritative.
        object->setVisible(true);
    }
    m_hidden.clear();

    if (layer) {
        layer->updateVisibility(0.f);
    }
}

void SmartDetailRenderer::apply(PlayLayer* layer, DetailTier tier) {
    if (!layer) {
        return;
    }
    if (!m_scanned) {
        scan(layer);
    }

    if (tier != m_lastTier && m_lastTier != DetailTier::Normal) {
        restore(layer);
    } else {
        // Hidden candidates are re-hidden after GD performs its own visibility
        // updates. Keep only the current frame's bookkeeping to avoid growth.
        m_hidden.clear();
    }

    if (tier == DetailTier::Normal) {
        layer->toggleGlitter(true);
        m_lastTier = tier;
        return;
    }

    layer->toggleGlitter(false);
    hideObjects(m_highDetail);
    if (tier == DetailTier::ULDM) {
        hideObjects(m_decoration);
    }

    m_lastTier = tier;
}

DetailRendererStats SmartDetailRenderer::stats() const {
    DetailRendererStats out;
    out.highDetailObjects = m_highDetail.size();
    out.decorationObjects = m_decoration.size();

    std::unordered_set<GameObject*> hidden;
    for (auto object : m_hidden) {
        if (object) hidden.insert(object);
    }
    out.hiddenObjects = hidden.size();
    return out;
}

} // namespace zaidultra
