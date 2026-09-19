#include "Whiteboard/protocol/command_codec.h"

#include <msgpack.hpp>

#include "Whiteboard/protocol/geometry_codec.h"
#include "Whiteboard/protocol/element_codec.h"
#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/element_commands.h"
#include "Whiteboard/command/full_sync_command.h"
#include "Whiteboard/command/transform_commands.h"
#include "Whiteboard/command/page_commands.h"
#include "Whiteboard/command/undo_redo_commands.h"
#include "Whiteboard/command/preview_commands.h"

namespace whiteboard {
namespace protocol {

std::string SerializeCommand(const Command& cmd)
{
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);

    pk.pack_array(3);
    pk.pack(cmd.GetType());
    pk.pack(cmd.pageId);

    if (auto* c = dynamic_cast<const StrokeBegin*>(&cmd))
    {
        pk.pack_array(4);
        pk.pack(c->strokeId);
        pk.pack(c->width);
        pk.pack(c->color);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const StrokeMove*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->strokeId);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const StrokeEnd*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->strokeId);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const EraserBegin*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->sessionId);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const EraserMove*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->sessionId);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const EraserEnd*>(&cmd))
    {
        pk.pack_array(4);
        pk.pack(c->sessionId);
        pk.pack(c->removedIds);
        pk.pack_array(static_cast<uint32_t>(c->addedElements.size()));
        for (const auto& e : c->addedElements)
            PackElement(pk, *e);
        // 与 addedElements 一一对齐的归属负载：每个 [parentId, cellIndex]
        pk.pack_array(static_cast<uint32_t>(c->addedPlacements.size()));
        for (const auto& p : c->addedPlacements)
        {
            pk.pack_array(2);
            pk.pack(p.parentId);
            pk.pack(p.cellIndex);
        }
    }
    else if (auto* c = dynamic_cast<const ElementAdd*>(&cmd))
    {
        pk.pack_array(1);
        PackElement(pk, *c->element);
    }
    else if (auto* c = dynamic_cast<const ElementRemove*>(&cmd))
    {
        pk.pack_array(1);
        pk.pack(c->elementId);
    }
    else if (auto* c = dynamic_cast<const ElementUpdate*>(&cmd))
    {
        pk.pack_array(1);
        PackElement(pk, *c->element);
    }
    else if (auto* c = dynamic_cast<const FullSync*>(&cmd))
    {
        pk.pack_array(1);
        pk.pack_array(static_cast<uint32_t>(c->pages.size()));
        for (const auto& page : c->pages)
            PackElement(pk, page);
    }
    else if (auto* c = dynamic_cast<const StrokeUpdate*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->strokeId);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const LassoPreview*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->sessionId);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const SelectionPreview*>(&cmd))
    {
        pk.pack_array(3);
        pk.pack(c->sessionId);
        pk.pack(c->points);
        pk.pack(c->selectedIds);
    }
    else if (auto* c = dynamic_cast<const PageCreate*>(&cmd))
    {
        pk.pack_array(1);
        pk.pack(c->newPageId);
    }
    else if (dynamic_cast<const PageSelect*>(&cmd) ||
             dynamic_cast<const PageDelete*>(&cmd) ||
             dynamic_cast<const PageClear*>(&cmd) ||
             dynamic_cast<const Undo*>(&cmd) ||
             dynamic_cast<const Redo*>(&cmd))
    {
        pk.pack_array(0);
    }
    else
    {
        pk.pack_array(0);
    }

    return std::string(sbuf.data(), sbuf.size());
}

std::shared_ptr<Command> DeserializeCommand(const std::string& data)
{
    msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
    const msgpack::object& obj = oh.get();

    if (obj.type != msgpack::type::ARRAY || obj.via.array.size != 3)
        throw msgpack::type_error();

    std::string type;
    obj.via.array.ptr[0].convert(type);

    auto cmd = CreateCommand(type);
    if (!cmd)
        return nullptr;

    obj.via.array.ptr[1].convert(cmd->pageId);

    const msgpack::object& payload = obj.via.array.ptr[2];
    if (payload.type != msgpack::type::ARRAY)
        throw msgpack::type_error();

    if (auto* c = dynamic_cast<StrokeBegin*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->strokeId);
        payload.via.array.ptr[1].convert(c->width);
        payload.via.array.ptr[2].convert(c->color);
        payload.via.array.ptr[3].convert(c->points);
    }
    else if (auto* c = dynamic_cast<StrokeMove*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->strokeId);
        payload.via.array.ptr[1].convert(c->points);
    }
    else if (auto* c = dynamic_cast<StrokeEnd*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->strokeId);
        payload.via.array.ptr[1].convert(c->points);
    }
    else if (auto* c = dynamic_cast<EraserBegin*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->sessionId);
        payload.via.array.ptr[1].convert(c->points);
    }
    else if (auto* c = dynamic_cast<EraserMove*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->sessionId);
        payload.via.array.ptr[1].convert(c->points);
    }
    else if (auto* c = dynamic_cast<EraserEnd*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->sessionId);
        payload.via.array.ptr[1].convert(c->removedIds);

        const msgpack::object& arr = payload.via.array.ptr[2];
        if (arr.type != msgpack::type::ARRAY)
            throw msgpack::type_error();

        c->addedElements.clear();
        for (uint32_t i = 0; i < arr.via.array.size; ++i)
        {
            auto e = UnpackElement(arr.via.array.ptr[i]);
            if (e)
                c->addedElements.push_back(e);
        }

        c->addedPlacements.clear();
        if (payload.via.array.size >= 4)
        {
            const msgpack::object& placements = payload.via.array.ptr[3];
            if (placements.type == msgpack::type::ARRAY)
            {
                for (uint32_t i = 0; i < placements.via.array.size; ++i)
                {
                    const msgpack::object& po = placements.via.array.ptr[i];
                    if (po.type != msgpack::type::ARRAY || po.via.array.size != 2)
                        continue;
                    EraserPlacement p;
                    po.via.array.ptr[0].convert(p.parentId);
                    po.via.array.ptr[1].convert(p.cellIndex);
                    c->addedPlacements.push_back(std::move(p));
                }
            }
        }
    }
    else if (auto* c = dynamic_cast<ElementAdd*>(cmd.get()))
    {
        c->element = UnpackElement(payload.via.array.ptr[0]);
    }
    else if (auto* c = dynamic_cast<ElementRemove*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->elementId);
    }
    else if (auto* c = dynamic_cast<ElementUpdate*>(cmd.get()))
    {
        c->element = UnpackElement(payload.via.array.ptr[0]);
    }
    else if (auto* c = dynamic_cast<FullSync*>(cmd.get()))
    {
        const msgpack::object& arr = payload.via.array.ptr[0];
        if (arr.type != msgpack::type::ARRAY)
            throw msgpack::type_error();

        c->pages.clear();
        for (uint32_t i = 0; i < arr.via.array.size; ++i)
        {
            auto page = std::dynamic_pointer_cast<Page>(UnpackElement(arr.via.array.ptr[i]));
            if (page)
                c->pages.push_back(*page);
        }
    }
    else if (auto* c = dynamic_cast<StrokeUpdate*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->strokeId);
        payload.via.array.ptr[1].convert(c->points);
    }
    else if (auto* c = dynamic_cast<LassoPreview*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->sessionId);
        payload.via.array.ptr[1].convert(c->points);
    }
    else if (auto* c = dynamic_cast<SelectionPreview*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->sessionId);
        payload.via.array.ptr[1].convert(c->points);
        payload.via.array.ptr[2].convert(c->selectedIds);
    }
    else if (auto* c = dynamic_cast<PageCreate*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->newPageId);
    }
    else if (dynamic_cast<PageSelect*>(cmd.get()) ||
             dynamic_cast<PageDelete*>(cmd.get()) ||
             dynamic_cast<PageClear*>(cmd.get()) ||
             dynamic_cast<Undo*>(cmd.get()) ||
             dynamic_cast<Redo*>(cmd.get()))
    {
        // 空负载：所有信息都在基类 pageId
    }

    return cmd;
}

} // namespace protocol
} // namespace whiteboard