#include "Whiteboard/protocol/command_codec.h"

#include <msgpack.hpp>

#include "Whiteboard/protocol/geometry_codec.h"
#include "Whiteboard/protocol/element_codec.h"
#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/element_commands.h"
#include "Whiteboard/command/full_sync_command.h"

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
        pk.pack(c->point);
    }
    else if (auto* c = dynamic_cast<const StrokeMove*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->strokeId);
        pk.pack(c->point);
    }
    else if (auto* c = dynamic_cast<const StrokeEnd*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->strokeId);
        pk.pack(c->point);
    }
    else if (auto* c = dynamic_cast<const EraserBegin*>(&cmd))
    {
        pk.pack_array(1);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const EraserMove*>(&cmd))
    {
        pk.pack_array(1);
        pk.pack(c->points);
    }
    else if (auto* c = dynamic_cast<const EraserEnd*>(&cmd))
    {
        pk.pack_array(2);
        pk.pack(c->removedIds);
        pk.pack_array(static_cast<uint32_t>(c->addedElements.size()));
        for (const auto& e : c->addedElements)
            PackElement(pk, *e);
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
    else if (auto* c = dynamic_cast<const FullSync*>(&cmd))
    {
        pk.pack_array(1);
        PackElement(pk, c->page);
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
        payload.via.array.ptr[3].convert(c->point);
    }
    else if (auto* c = dynamic_cast<StrokeMove*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->strokeId);
        payload.via.array.ptr[1].convert(c->point);
    }
    else if (auto* c = dynamic_cast<StrokeEnd*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->strokeId);
        payload.via.array.ptr[1].convert(c->point);
    }
    else if (auto* c = dynamic_cast<EraserBegin*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->points);
    }
    else if (auto* c = dynamic_cast<EraserMove*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->points);
    }
    else if (auto* c = dynamic_cast<EraserEnd*>(cmd.get()))
    {
        payload.via.array.ptr[0].convert(c->removedIds);

        const msgpack::object& arr = payload.via.array.ptr[1];
        if (arr.type != msgpack::type::ARRAY)
            throw msgpack::type_error();

        c->addedElements.clear();
        for (uint32_t i = 0; i < arr.via.array.size; ++i)
        {
            auto e = UnpackElement(arr.via.array.ptr[i]);
            if (e)
                c->addedElements.push_back(e);
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
    else if (auto* c = dynamic_cast<FullSync*>(cmd.get()))
    {
        auto page = std::dynamic_pointer_cast<Page>(UnpackElement(payload.via.array.ptr[0]));
        if (page)
            c->page = *page;
    }

    return cmd;
}

} // namespace protocol
} // namespace whiteboard