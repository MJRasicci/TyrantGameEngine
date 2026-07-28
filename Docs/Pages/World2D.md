# World2D and two-dimensional drawing

Tyrant's 2D stack separates portable resource contracts, immediate command
recording, and retained world state into three modules:

```text
Rendering
  -> Drawing2D
       -> World2D
```

Applications may include each layer independently with `TGE/Rendering.hpp`,
`TGE/Drawing2D.hpp`, and `TGE/World2D.hpp`.

## Rendering resources

Rendering defines linear and sRGB colors, decoded CPU images, pixel formats,
blend state, and opaque handles for textures, texture views, samplers, shader
programs, materials, material instances, and render targets. Descriptors are
backend-neutral and have explicit validation functions.

`IRenderingDevice` is the creation, upload, liveness, and destruction boundary.
All handles from one device share its domain. A nonzero numeric handle is not
proof that a resource exists; a device registry remains authoritative.
Destroying a handle prevents future submission, while a backend must retain its
native allocation until already-submitted work completes.

`RenderTexture2D` groups an offscreen target with its sampleable texture and
view. It intentionally does not hide multisample resolve behavior.

SPIR-V is the engine's canonical shader IR. A non-Vulkan renderer is responsible
for translating or cross-compiling it when creating a program.

## Drawing values and commands

Drawing2D provides:

- exact affine matrices and convenient transform values with a uniform pivot;
- orthographic views and normalized viewports;
- sprites and texture regions;
- UTF-8 text with externally shaped glyph layouts;
- rectangles, circles, validated convex polygons, fills, and strokes;
- validated indexed point, line, and triangle geometry; and
- owning CPU command variants recorded by `RenderPass2D`.

Drawables do not inherit from a stateful base class. Placement belongs to the
draw command or a retained visual attachment. Custom values can satisfy the
`Drawable2D` protocol by expanding themselves into built-in commands.

World coordinates use positive Y upward. Pixel coordinates and normalized
viewports use a top-left origin with positive Y downward. `LinearColor` uses
linear-light channels and straight alpha; premultiplication is explicit.

`DrawState2D` uses null resource handles as renderer defaults. A null material
selects the built-in material for the drawable, a null sampler selects the
built-in sampler, and a missing blend override inherits the selected
material's blend state. Pixel snapping remains a submission-time operation
because it depends on the target extent and resolved viewport.

Calling `RenderPass2D::Validate()` catches malformed drawables, non-finite
transforms, invalid blend overrides, and cross-device resources.
`IRenderer2D::Submit()` is the backend execution boundary and additionally
checks registry liveness and retains resources through execution.

The existing private Linux Vulkan presentation adapter still performs
clear-and-present only. It does not yet implement `IRenderingDevice` or
`IRenderer2D`; the new contracts keep that backend work separate from the
portable world and drawing model.

## Text and glyph atlases

`Font` is a domain-qualified handle owned by an `ITextService2D`.
The service loads faces, shapes UTF-8, and produces a ready `TextLayout2D`.
Visible shaped glyphs carry texture regions in service-owned glyph atlases.

Changing a text string or style invalidates its old layout. The value remains
semantically valid but is not renderable until the text service supplies a new
ready layout. Pass validation rejects unlaid text instead of silently drawing
an empty string.

## Retained world

`World2D` is a single-writer retained hierarchy. Nodes hold local transforms
and enabled state. Independently identified visual attachments hold a sprite,
text value, shape, or custom geometry plus local visual transform, draw state,
painter order, and visibility mask.

Identifiers are domain-qualified and generational. Stale identifiers and
identifiers issued by another world return errors instead of addressing reused
storage. Parenting rejects cycles. Reparenting can preserve local placement or
world placement; operations that would require an unrepresentable local shear
fail atomically.

A typical frame extracts and records state as follows:

```cpp
TGE::World2D world;
auto node = world.CreateNode({
    .translation = { 20.0F, 30.0F }
});
if (!node)
{
    // Handle node.error().
}

auto visual = world.AddRectangle(
    *node,
    TGE::Rectangle2D { .size = { 64.0F, 32.0F } });

auto scene = world.PublishRenderScene();
auto pass = TGE::RenderPass2D::Create({
    .target = target,
    .view = view
});
if (visual && pass)
{
    scene.Record(*pass);
    auto validation = pass->Validate();
    if (validation)
    {
        renderer.Submit(*pass);
    }
}
```

`PublishRenderScene()` returns an immutable, copyable CPU snapshot. It
preserves exact affine hierarchy composition, copies semantic drawable data,
filters disabled and hidden visuals, and sorts by layer, order, then stable
insertion sequence. Recording applies a visibility mask and conservative view
culling.

Snapshots and passes own their CPU values, but numeric renderer and font
handles do not extend external resource lifetime.

## Threading

World mutation is intentionally single-writer. Published scenes are immutable
and may be retained or handed to another thread, subject to the lifetime of
their external renderer and font resources. Concrete devices and renderers
define their own submission-thread contract.
