#include "pch.h"

#include "format5.h"

#include "addresslibrary5.h"

// The pinned (MIT) CommonLib predates 1.7: it takes 1.7 for 1.5.x and can't read format 5. Both are settled
// here, so that the submodule stays unmodified. Newer CommonLibs do this themselves but are GPL-3.0.
//
// That takes four private members. Access checks don't apply to the arguments of an explicit template
// instantiation, which is what Access is for. If the submodule moves and they're renamed, this won't compile.

namespace
{
    struct Key
    {
        friend void Publish(Key, const std::vector<std::uint32_t>& a_offsets);
    };

    template <auto RuntimeMember, auto Database, auto Initialized, auto TableMember>
    struct Access
    {
        friend void Publish(Key, const std::vector<std::uint32_t>& a_offsets)
        {
            using Table = std::remove_cvref_t<decltype((*Database).*TableMember)>;
            static std::vector<typename Table::value_type> mappings;  // the database points into this

            // One entry per ID, unused ones included, as CommonLib's search takes the next ID up for a missing one.
            mappings.resize(a_offsets.size());
            for (std::size_t id = 0; id < a_offsets.size(); ++id) {
                mappings[id] = { id, a_offsets[id] };
            }

            REL::Module::get().*RuntimeMember = REL::Module::Runtime::AE;  // 1.7 uses the AE IDs
            (*Database).*TableMember = Table{ mappings };
            Initialized->store(true);  // so get() doesn't go and load the file itself
        }
    };

    template struct Access<
        &REL::Module::_runtime,
        &REL::IDDatabase::_instance,
        &REL::IDDatabase::_initialized,
        &REL::IDDatabase::_id2offset>;
}

namespace Format5
{
    const char* Install(std::span<const std::uint32_t> a_required)
    {
        std::ifstream file{ R"(.\Data\SKSE\Plugins\versionlib-1-7-104-0.bin)", std::ios::binary };
        const auto    table = AddressLibrary5::Read(file, { 1, 7, 104, 0 }, a_required);
        if (table.error) {
            return table.error;
        }
        Publish(Key{}, table.offsets);
        return nullptr;
    }
}
