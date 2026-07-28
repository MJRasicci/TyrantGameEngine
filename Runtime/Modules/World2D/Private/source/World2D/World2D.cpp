#include "TGE/World2D/World2D.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <utility>
#include <vector>

#include "Internal/World2D/RenderScene2DState.hpp"

namespace TGE
{
    namespace
    {
        std::atomic<std::uint64_t> NextWorldDomain { 1 };

        World2DError NodeNotFound(Node2DId id)
        {
            return {
                .code = World2DErrorCode::NodeNotFound,
                .message = std::format(
                    "World2D node {} is invalid, stale, or belongs to another "
                    "world.",
                    id.Value())
            };
        }

        World2DError VisualNotFound(Visual2DId id)
        {
            return {
                .code = World2DErrorCode::VisualNotFound,
                .message = std::format(
                    "World2D visual {} is invalid, stale, or belongs to another "
                    "world.",
                    id.Value())
            };
        }

        std::uint32_t NextGeneration(std::uint32_t generation) noexcept
        {
            ++generation;
            return generation == 0 ? 1 : generation;
        }

        std::optional<Transform2D> Decompose(
            const AffineTransform2D& value,
            float epsilon = 1.0e-4F) noexcept
        {
            if (!value.IsFinite())
            {
                return std::nullopt;
            }

            const auto firstColumnLength =
                std::hypot(value.M00(), value.M10());
            const auto secondColumnLength =
                std::hypot(value.M01(), value.M11());
            if (!std::isfinite(firstColumnLength) ||
                !std::isfinite(secondColumnLength))
            {
                return std::nullopt;
            }

            float rotation = 0.0F;
            float scaleX = 0.0F;
            float scaleY = 0.0F;
            if (firstColumnLength > epsilon)
            {
                scaleX = firstColumnLength;
                rotation = std::atan2(value.M10(), value.M00());
                scaleY = value.Determinant() / scaleX;
            }
            else if (secondColumnLength > epsilon)
            {
                scaleY = secondColumnLength;
                rotation = std::atan2(-value.M01(), value.M11());
            }

            const auto cosine = std::cos(rotation);
            const auto sine = std::sin(rotation);

            const auto expected00 = cosine * scaleX;
            const auto expected10 = sine * scaleX;
            const auto expected01 = -sine * scaleY;
            const auto expected11 = cosine * scaleY;
            const auto close = [epsilon](float left, float right)
            {
                const auto magnitude =
                    std::max({ 1.0F, std::abs(left), std::abs(right) });
                return std::abs(left - right) <= epsilon * magnitude;
            };

            if (!close(value.M00(), expected00) ||
                !close(value.M10(), expected10) ||
                !close(value.M01(), expected01) ||
                !close(value.M11(), expected11))
            {
                return std::nullopt;
            }

            return Transform2D {
                .translation = { value.M02(), value.M12() },
                .rotation = Angle::FromRadians(rotation),
                .scale = { scaleX, scaleY }
            };
        }

        bool IsValidVisual(const Visual2D& value) noexcept
        {
            return std::visit(
                [](const auto& visual)
                {
                    return visual.IsValid();
                },
                value);
        }
    }

    struct World2D::State
    {
        static constexpr std::uint32_t InvalidSlot =
            std::numeric_limits<std::uint32_t>::max();

        struct NodeRecord
        {
            std::uint32_t generation { 1 };
            bool alive { false };
            bool enabled { true };
            Transform2D transform {};
            std::optional<std::uint32_t> parent;
            std::vector<std::uint32_t> children;
            std::vector<std::uint32_t> visuals;
            std::uint32_t nextFree { InvalidSlot };
        };

        struct VisualRecord
        {
            std::uint32_t generation { 1 };
            bool alive { false };
            std::uint32_t node { 0 };
            Visual2D value;
            VisualProperties2D properties;
            std::uint64_t stableSequence { 0 };
            std::uint32_t nextFree { InvalidSlot };
        };

        [[nodiscard]] static std::uint64_t Encode(
            std::uint32_t slot,
            std::uint32_t generation) noexcept
        {
            return (static_cast<std::uint64_t>(generation) << 32) |
                   (static_cast<std::uint64_t>(slot) + 1);
        }

        [[nodiscard]] static std::optional<std::uint32_t> Slot(
            std::uint64_t value) noexcept
        {
            const auto encoded = static_cast<std::uint32_t>(value);
            if (encoded == 0)
            {
                return std::nullopt;
            }
            return encoded - 1;
        }

        [[nodiscard]] static std::uint32_t Generation(
            std::uint64_t value) noexcept
        {
            return static_cast<std::uint32_t>(value >> 32);
        }

        [[nodiscard]] NodeRecord* Find(Node2DId id) noexcept
        {
            if (id.DomainValue() != domain)
            {
                return nullptr;
            }
            const auto slot = Slot(id.Value());
            if (!slot || *slot >= nodes.size())
            {
                return nullptr;
            }
            auto& record = nodes[*slot];
            return record.alive &&
                   record.generation == Generation(id.Value())
                ? &record
                : nullptr;
        }

        [[nodiscard]] const NodeRecord* Find(Node2DId id) const noexcept
        {
            return const_cast<State*>(this)->Find(id);
        }

        [[nodiscard]] VisualRecord* Find(Visual2DId id) noexcept
        {
            if (id.DomainValue() != domain)
            {
                return nullptr;
            }
            const auto slot = Slot(id.Value());
            if (!slot || *slot >= visuals.size())
            {
                return nullptr;
            }
            auto& record = visuals[*slot];
            return record.alive &&
                   record.generation == Generation(id.Value())
                ? &record
                : nullptr;
        }

        [[nodiscard]] const VisualRecord* Find(
            Visual2DId id) const noexcept
        {
            return const_cast<State*>(this)->Find(id);
        }

        [[nodiscard]] std::uint32_t NodeSlot(Node2DId id) const noexcept
        {
            return *Slot(id.Value());
        }

        [[nodiscard]] std::uint32_t VisualSlot(
            Visual2DId id) const noexcept
        {
            return *Slot(id.Value());
        }

        [[nodiscard]] Node2DId AllocateNode(Transform2D transform)
        {
            std::uint32_t slot;
            if (!freeNodeHead)
            {
                slot = static_cast<std::uint32_t>(nodes.size());
                nodes.emplace_back();
            }
            else
            {
                slot = *freeNodeHead;
                const auto next = nodes[slot].nextFree;
                freeNodeHead = next == InvalidSlot
                    ? std::nullopt
                    : std::optional<std::uint32_t> { next };
            }

            auto& record = nodes[slot];
            record.alive = true;
            record.enabled = true;
            record.transform = transform;
            record.parent.reset();
            record.children.clear();
            record.visuals.clear();
            record.nextFree = InvalidSlot;
            ++revision;
            return Node2DId::FromValues(
                domain,
                Encode(slot, record.generation));
        }

        [[nodiscard]] World2DResult<Visual2DId> AllocateVisual(
            Node2DId nodeId,
            Visual2D value,
            VisualProperties2D properties)
        {
            auto* node = Find(nodeId);
            if (!node)
            {
                return std::unexpected(NodeNotFound(nodeId));
            }
            if (!properties.localTransform.IsFinite())
            {
                return std::unexpected(World2DError {
                    .code = World2DErrorCode::InvalidDescriptor,
                    .message =
                        "A World2D visual requires a finite local transform."
                });
            }
            if (!IsValidVisual(value))
            {
                return std::unexpected(World2DError {
                    .code = World2DErrorCode::InvalidDescriptor,
                    .message =
                        "A World2D visual requires valid drawable data."
                });
            }

            node->visuals.reserve(node->visuals.size() + 1);

            const bool reuseSlot = freeVisualHead.has_value();
            const auto slot = reuseSlot
                ? *freeVisualHead
                : static_cast<std::uint32_t>(visuals.size());
            const auto generation = reuseSlot
                ? visuals[slot].generation
                : std::uint32_t { 1 };
            const auto nextFree = reuseSlot
                ? visuals[slot].nextFree
                : InvalidSlot;
            VisualRecord replacement {
                .generation = generation,
                .alive = true,
                .node = NodeSlot(nodeId),
                .value = std::move(value),
                .properties = std::move(properties),
                .stableSequence = nextVisualSequence,
                .nextFree = InvalidSlot
            };

            if (reuseSlot)
            {
                visuals[slot] = std::move(replacement);
            }
            else
            {
                visuals.emplace_back(std::move(replacement));
            }
            node->visuals.emplace_back(slot);
            if (reuseSlot)
            {
                freeVisualHead = nextFree == InvalidSlot
                    ? std::nullopt
                    : std::optional<std::uint32_t> { nextFree };
            }
            ++nextVisualSequence;
            ++revision;
            return Visual2DId::FromValues(
                domain,
                Encode(slot, visuals[slot].generation));
        }

        void InvalidateVisual(std::uint32_t slot) noexcept
        {
            auto& record = visuals[slot];
            if (!record.alive)
            {
                return;
            }
            record.alive = false;
            record.generation = NextGeneration(record.generation);
            record.stableSequence = 0;
            record.nextFree = freeVisualHead.value_or(InvalidSlot);
            freeVisualHead = slot;
        }

        void RemoveVisualFromNode(std::uint32_t visualSlot) noexcept
        {
            const auto nodeSlot = visuals[visualSlot].node;
            auto& attached = nodes[nodeSlot].visuals;
            std::erase(attached, visualSlot);
        }

        void InvalidateNode(std::uint32_t slot) noexcept
        {
            auto& record = nodes[slot];
            if (!record.alive)
            {
                return;
            }
            for (const auto visual : record.visuals)
            {
                InvalidateVisual(visual);
            }
            record.visuals.clear();
            record.children.clear();
            record.parent.reset();
            record.alive = false;
            record.enabled = false;
            record.generation = NextGeneration(record.generation);
            record.nextFree = freeNodeHead.value_or(InvalidSlot);
            freeNodeHead = slot;
        }

        [[nodiscard]] AffineTransform2D ResolveWorld(
            std::uint32_t slot) const noexcept
        {
            std::vector<std::uint32_t> chain;
            for (auto current = std::optional<std::uint32_t> { slot };
                 current;
                 current = nodes[*current].parent)
            {
                chain.emplace_back(*current);
            }

            auto result = AffineTransform2D::Identity();
            for (auto found = chain.rbegin(); found != chain.rend(); ++found)
            {
                result = result * nodes[*found].transform.Matrix();
            }
            return result;
        }

        [[nodiscard]] bool ResolveEnabled(
            std::uint32_t slot) const noexcept
        {
            for (auto current = std::optional<std::uint32_t> { slot };
                 current;
                 current = nodes[*current].parent)
            {
                if (!nodes[*current].enabled)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool WouldCreateCycle(
            std::uint32_t child,
            std::optional<std::uint32_t> parent) const noexcept
        {
            for (auto current = parent;
                 current;
                 current = nodes[*current].parent)
            {
                if (*current == child)
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<NodeRecord> nodes;
        std::optional<std::uint32_t> freeNodeHead;
        std::vector<VisualRecord> visuals;
        std::optional<std::uint32_t> freeVisualHead;
        std::uint64_t revision { 0 };
        std::uint64_t nextVisualSequence { 1 };
        std::uint64_t domain { NextWorldDomain.fetch_add(1) };
    };

    World2D::World2D()
        : state(std::make_unique<State>())
    {
    }

    World2D::~World2D() = default;
    World2D::World2D(World2D&&) noexcept = default;
    World2D& World2D::operator=(World2D&&) noexcept = default;

    World2DResult<Node2DId> World2D::CreateNode(Transform2D transform)
    {
        if (!transform.IsFinite())
        {
            return std::unexpected(World2DError {
                .code = World2DErrorCode::InvalidDescriptor,
                .message = "A World2D node requires a finite transform."
            });
        }
        if (!state)
        {
            state = std::make_unique<State>();
        }
        return state->AllocateNode(transform);
    }

    World2DResult<void> World2D::DestroyNode(
        Node2DId nodeId,
        NodeDestroyPolicy policy)
    {
        auto* node = state ? state->Find(nodeId) : nullptr;
        if (!node)
        {
            return std::unexpected(NodeNotFound(nodeId));
        }

        const auto slot = state->NodeSlot(nodeId);
        if (policy == NodeDestroyPolicy::DestroySubtree)
        {
            std::vector<std::uint32_t> pending { slot };
            std::vector<std::uint32_t> subtree;
            while (!pending.empty())
            {
                const auto current = pending.back();
                pending.pop_back();
                subtree.emplace_back(current);
                pending.insert(
                    pending.end(),
                    state->nodes[current].children.begin(),
                    state->nodes[current].children.end());
            }

            if (node->parent)
            {
                std::erase(state->nodes[*node->parent].children, slot);
            }
            for (auto found = subtree.rbegin();
                 found != subtree.rend();
                 ++found)
            {
                state->InvalidateNode(*found);
            }
            ++state->revision;
            return {};
        }

        const auto replacementParent = node->parent;
        std::vector<std::pair<std::uint32_t, Transform2D>> replacements;
        replacements.reserve(node->children.size());

        for (const auto child : node->children)
        {
            const auto desiredLocal =
                node->transform.Matrix() *
                state->nodes[child].transform.Matrix();
            auto decomposed = Decompose(desiredLocal);
            if (!decomposed)
            {
                return std::unexpected(World2DError {
                    .code = World2DErrorCode::NonDecomposableTransform,
                    .message =
                        "Reparenting a child while destroying its parent would "
                        "introduce shear that Transform2D cannot represent."
                });
            }
            replacements.emplace_back(child, *decomposed);
        }

        if (replacementParent)
        {
            auto& siblings = state->nodes[*replacementParent].children;
            siblings.reserve(
                siblings.size() - 1 + replacements.size());
            std::erase(siblings, slot);
            for (const auto& [child, transform] : replacements)
            {
                (void)transform;
                siblings.emplace_back(child);
            }
        }
        for (const auto& [child, transform] : replacements)
        {
            auto& childNode = state->nodes[child];
            childNode.parent = replacementParent;
            childNode.transform = transform;
        }

        state->InvalidateNode(slot);
        ++state->revision;
        return {};
    }

    bool World2D::Contains(Node2DId node) const noexcept
    {
        return state && state->Find(node) != nullptr;
    }

    bool World2D::Contains(Visual2DId visual) const noexcept
    {
        return state && state->Find(visual) != nullptr;
    }

    std::uint64_t World2D::Revision() const noexcept
    {
        return state ? state->revision : 0;
    }

    World2DResult<Transform2D> World2D::Transform(
        Node2DId node) const
    {
        const auto* record = state ? state->Find(node) : nullptr;
        if (!record)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return record->transform;
    }

    World2DResult<AffineTransform2D> World2D::WorldTransform(
        Node2DId node) const
    {
        if (!state || !state->Find(node))
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->ResolveWorld(state->NodeSlot(node));
    }

    World2DResult<void> World2D::SetTransform(
        Node2DId node,
        Transform2D transform)
    {
        auto* record = state ? state->Find(node) : nullptr;
        if (!record)
        {
            return std::unexpected(NodeNotFound(node));
        }
        if (!transform.IsFinite())
        {
            return std::unexpected(World2DError {
                .code = World2DErrorCode::InvalidDescriptor,
                .message = "A World2D node requires a finite transform."
            });
        }
        record->transform = transform;
        ++state->revision;
        return {};
    }

    World2DResult<void> World2D::SetEnabled(
        Node2DId node,
        bool enabled)
    {
        auto* record = state ? state->Find(node) : nullptr;
        if (!record)
        {
            return std::unexpected(NodeNotFound(node));
        }
        if (record->enabled != enabled)
        {
            record->enabled = enabled;
            ++state->revision;
        }
        return {};
    }

    World2DResult<void> World2D::SetParent(
        Node2DId childId,
        std::optional<Node2DId> parentId,
        ReparentMode mode)
    {
        auto* child = state ? state->Find(childId) : nullptr;
        if (!child)
        {
            return std::unexpected(NodeNotFound(childId));
        }

        std::optional<std::uint32_t> parentSlot;
        if (parentId)
        {
            if (!state->Find(*parentId))
            {
                return std::unexpected(NodeNotFound(*parentId));
            }
            parentSlot = state->NodeSlot(*parentId);
        }

        const auto childSlot = state->NodeSlot(childId);
        if (state->WouldCreateCycle(childSlot, parentSlot))
        {
            return std::unexpected(World2DError {
                .code = World2DErrorCode::HierarchyCycle,
                .message =
                    "A World2D node cannot be parented to itself or one of its "
                    "descendants."
            });
        }
        if (child->parent == parentSlot)
        {
            return {};
        }

        auto replacementTransform = child->transform;
        if (mode == ReparentMode::KeepWorld)
        {
            const auto oldWorld = state->ResolveWorld(childSlot);
            const auto parentWorld = parentSlot
                ? state->ResolveWorld(*parentSlot)
                : AffineTransform2D::Identity();
            const auto parentInverse = parentWorld.Inverse();
            if (!parentInverse)
            {
                return std::unexpected(World2DError {
                    .code = World2DErrorCode::NonInvertibleTransform,
                    .message =
                        "The requested parent has a non-invertible world "
                        "transform."
                });
            }
            auto decomposed = Decompose(*parentInverse * oldWorld);
            if (!decomposed)
            {
                return std::unexpected(World2DError {
                    .code = World2DErrorCode::NonDecomposableTransform,
                    .message =
                        "Keeping the node's world transform would introduce "
                        "shear that Transform2D cannot represent."
                });
            }
            replacementTransform = *decomposed;
        }

        if (parentSlot)
        {
            auto& newSiblings = state->nodes[*parentSlot].children;
            newSiblings.reserve(newSiblings.size() + 1);
        }
        if (child->parent)
        {
            std::erase(
                state->nodes[*child->parent].children,
                childSlot);
        }
        child->parent = parentSlot;
        child->transform = replacementTransform;
        if (parentSlot)
        {
            state->nodes[*parentSlot].children.emplace_back(childSlot);
        }
        ++state->revision;
        return {};
    }

    World2DResult<Visual2DId> World2D::AddSprite(
        Node2DId node,
        Sprite2D sprite,
        VisualProperties2D properties)
    {
        if (!state)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->AllocateVisual(
            node,
            Visual2D { std::in_place_type<Sprite2D>, std::move(sprite) },
            std::move(properties));
    }

    World2DResult<Visual2DId> World2D::AddText(
        Node2DId node,
        Text2D text,
        VisualProperties2D properties)
    {
        if (!state)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->AllocateVisual(
            node,
            Visual2D { std::in_place_type<Text2D>, std::move(text) },
            std::move(properties));
    }

    World2DResult<Visual2DId> World2D::AddRectangle(
        Node2DId node,
        Rectangle2D rectangle,
        VisualProperties2D properties)
    {
        if (!state)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->AllocateVisual(
            node,
            Visual2D {
                std::in_place_type<Rectangle2D>,
                std::move(rectangle)
            },
            std::move(properties));
    }

    World2DResult<Visual2DId> World2D::AddCircle(
        Node2DId node,
        Circle2D circle,
        VisualProperties2D properties)
    {
        if (!state)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->AllocateVisual(
            node,
            Visual2D { std::in_place_type<Circle2D>, std::move(circle) },
            std::move(properties));
    }

    World2DResult<Visual2DId> World2D::AddConvexPolygon(
        Node2DId node,
        ConvexPolygon2D polygon,
        VisualProperties2D properties)
    {
        if (!state)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->AllocateVisual(
            node,
            Visual2D {
                std::in_place_type<ConvexPolygon2D>,
                std::move(polygon)
            },
            std::move(properties));
    }

    World2DResult<Visual2DId> World2D::AddGeometry(
        Node2DId node,
        Geometry2D geometry,
        VisualProperties2D properties)
    {
        if (!state)
        {
            return std::unexpected(NodeNotFound(node));
        }
        return state->AllocateVisual(
            node,
            Visual2D { std::in_place_type<Geometry2D>, std::move(geometry) },
            std::move(properties));
    }

    World2DResult<void> World2D::SetVisual(
        Visual2DId visual,
        Visual2D value)
    {
        auto* record = state ? state->Find(visual) : nullptr;
        if (!record)
        {
            return std::unexpected(VisualNotFound(visual));
        }
        if (record->value.index() != value.index())
        {
            return std::unexpected(World2DError {
                .code = World2DErrorCode::VisualTypeMismatch,
                .message =
                    "SetVisual cannot replace a retained visual with a "
                    "different visual type."
            });
        }
        if (!IsValidVisual(value))
        {
            return std::unexpected(World2DError {
                .code = World2DErrorCode::InvalidDescriptor,
                .message =
                    "A World2D visual requires valid drawable data."
            });
        }
        record->value = std::move(value);
        ++state->revision;
        return {};
    }

    World2DResult<void> World2D::SetVisualProperties(
        Visual2DId visual,
        VisualProperties2D properties)
    {
        auto* record = state ? state->Find(visual) : nullptr;
        if (!record)
        {
            return std::unexpected(VisualNotFound(visual));
        }
        if (!properties.localTransform.IsFinite())
        {
            return std::unexpected(World2DError {
                .code = World2DErrorCode::InvalidDescriptor,
                .message =
                    "A World2D visual requires a finite local transform."
            });
        }
        record->properties = std::move(properties);
        ++state->revision;
        return {};
    }

    World2DResult<VisualProperties2D> World2D::VisualProperties(
        Visual2DId visual) const
    {
        const auto* record = state ? state->Find(visual) : nullptr;
        if (!record)
        {
            return std::unexpected(VisualNotFound(visual));
        }
        return record->properties;
    }

    World2DResult<void> World2D::RemoveVisual(Visual2DId visual)
    {
        auto* record = state ? state->Find(visual) : nullptr;
        if (!record)
        {
            return std::unexpected(VisualNotFound(visual));
        }
        const auto slot = state->VisualSlot(visual);
        state->RemoveVisualFromNode(slot);
        state->InvalidateVisual(slot);
        ++state->revision;
        return {};
    }

    RenderScene2D World2D::PublishRenderScene() const
    {
        if (!state)
        {
            return {};
        }
        auto snapshot = std::make_shared<RenderScene2D::State>();
        snapshot->revision = state->revision;
        snapshot->items.reserve(state->visuals.size());

        for (std::uint32_t slot = 0;
             slot < state->visuals.size();
             ++slot)
        {
            const auto& visual = state->visuals[slot];
            if (!visual.alive ||
                !visual.properties.visible ||
                !state->ResolveEnabled(visual.node))
            {
                continue;
            }

            const auto nodeWorld = state->ResolveWorld(visual.node);
            const auto world =
                nodeWorld * visual.properties.localTransform.Matrix();
            const auto localBounds = std::visit(
                [](const auto& value)
                {
                    return value.LocalBounds();
                },
                visual.value);

            snapshot->items.emplace_back(RenderItem2D {
                .node = Node2DId::FromValues(
                    state->domain,
                    State::Encode(
                        visual.node,
                        state->nodes[visual.node].generation)),
                .visual = Visual2DId::FromValues(
                    state->domain,
                    State::Encode(slot, visual.generation)),
                .value = visual.value,
                .worldTransform = world,
                .worldBounds = world.TransformBounds(localBounds),
                .drawState = visual.properties.drawState,
                .drawOrder = visual.properties.drawOrder,
                .visibilityMask = visual.properties.visibilityMask,
                .stableSequence = visual.stableSequence
            });
        }

        std::ranges::stable_sort(
            snapshot->items,
            [](const RenderItem2D& left, const RenderItem2D& right)
            {
                if (left.drawOrder != right.drawOrder)
                {
                    return left.drawOrder < right.drawOrder;
                }
                return left.stableSequence < right.stableSequence;
            });

        return RenderScene2D(std::move(snapshot));
    }
}
