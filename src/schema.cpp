////////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include "schema.hpp"

#include "object_schema.hpp"
#include "object_store.hpp"
#include "object_schema.hpp"
#include "property.hpp"

#include <algorithm>

using namespace realm;

namespace realm {
bool operator==(Schema const& a, Schema const& b)
{
    return static_cast<Schema::base const&>(a) == static_cast<Schema::base const&>(b);
}
}

Schema::Schema() = default;
Schema::~Schema() = default;
Schema::Schema(Schema const&) = default;
Schema::Schema(Schema &&) = default;
Schema& Schema::operator=(Schema const&) = default;
Schema& Schema::operator=(Schema&&) = default;

Schema::Schema(std::initializer_list<ObjectSchema> types) : Schema(base(types)) { }

Schema::Schema(base types) : base(std::move(types))
{
    std::sort(begin(), end(), [](ObjectSchema const& lft, ObjectSchema const& rgt) {
        return lft.name < rgt.name;
    });
}

Schema::iterator Schema::find(StringData name)
{
    auto it = std::lower_bound(begin(), end(), name, [](ObjectSchema const& lft, StringData rgt) {
        return lft.name < rgt;
    });
    if (it != end() && it->name != name) {
        it = end();
    }
    return it;
}

Schema::const_iterator Schema::find(StringData name) const
{
    return const_cast<Schema *>(this)->find(name);
}

Schema::iterator Schema::find(ObjectSchema const& object) noexcept
{
    return find(object.name);
}

Schema::const_iterator Schema::find(ObjectSchema const& object) const noexcept
{
    return const_cast<Schema *>(this)->find(object);
}

void Schema::validate() const
{
    std::vector<ObjectSchemaValidationException> exceptions;
    for (auto const& object : *this) {
        object.validate(*this, exceptions);
    }

    if (exceptions.size()) {
        throw SchemaValidationException(exceptions);
    }
}

static void compare(ObjectSchema const& existing_schema,
                    ObjectSchema const& target_schema,
                    std::vector<SchemaChange>& changes)
{
    for (auto& current_prop : existing_schema.persisted_properties) {
        auto target_prop = target_schema.property_for_name(current_prop.name);

        if (!target_prop) {
            changes.emplace_back(schema_change::RemoveProperty{&existing_schema, &current_prop});
            continue;
        }
        if (current_prop.type != target_prop->type || current_prop.object_type != target_prop->object_type) {
            changes.emplace_back(schema_change::ChangePropertyType{&existing_schema, &current_prop, target_prop});
            continue;
        }
        if (current_prop.is_nullable != target_prop->is_nullable) {
            if (current_prop.is_nullable)
                changes.emplace_back(schema_change::MakePropertyRequired{&existing_schema, &current_prop});
            else
                changes.emplace_back(schema_change::MakePropertyNullable{&existing_schema, &current_prop});
        }
        if (target_prop->requires_index()) {
            if (!current_prop.is_indexed)
                changes.emplace_back(schema_change::AddIndex{&existing_schema, &current_prop});
        }
        else if (current_prop.requires_index()) {
            changes.emplace_back(schema_change::RemoveIndex{&existing_schema, &current_prop});
        }
    }

    if (existing_schema.primary_key != target_schema.primary_key) {
        changes.emplace_back(schema_change::ChangePrimaryKey{&existing_schema, target_schema.primary_key_property()});
    }

    for (auto& target_prop : target_schema.persisted_properties) {
        if (!existing_schema.property_for_name(target_prop.name)) {
            changes.emplace_back(schema_change::AddProperty{&existing_schema, &target_prop});
        }
    }
}

std::vector<SchemaChange> Schema::compare(Schema const& target_schema) const
{
    std::vector<SchemaChange> changes;
    for (auto &object_schema : target_schema) {
        auto matching_schema = find(object_schema);
        if (matching_schema == end()) {
            changes.emplace_back(schema_change::AddTable{&object_schema});
            continue;
        }

        ::compare(*matching_schema, object_schema, changes);
    }
    return changes;
}

void Schema::copy_table_columns_from(realm::Schema const& other)
{
    for (auto& source_schema : other) {
        auto matching_schema = find(source_schema);
        if (matching_schema == end()) {
            continue;
        }

        for (auto& current_prop : source_schema.persisted_properties) {
            auto target_prop = matching_schema->property_for_name(current_prop.name);
            if (target_prop) {
                target_prop->table_column = current_prop.table_column;
            }
        }
    }
}

namespace realm {
bool operator==(SchemaChange const& lft, SchemaChange const& rgt)
{
    if (lft.m_kind != rgt.m_kind)
        return false;

    using namespace schema_change;
    struct Visitor {
        SchemaChange const& value;

        #define REALM_SC_COMPARE(type, ...) \
            bool operator()(type rgt) const \
            { \
                auto cmp = [](auto&& v) { return std::tie(__VA_ARGS__); }; \
                return cmp(value.type) == cmp(rgt); \
            }

        REALM_SC_COMPARE(AddIndex, v.object, v.property)
        REALM_SC_COMPARE(AddProperty, v.object, v.property)
        REALM_SC_COMPARE(AddTable, v.object)
        REALM_SC_COMPARE(ChangePrimaryKey, v.object, v.property)
        REALM_SC_COMPARE(ChangePropertyType, v.object, v.old_property, v.new_property)
        REALM_SC_COMPARE(MakePropertyNullable, v.object, v.property)
        REALM_SC_COMPARE(MakePropertyRequired, v.object, v.property)
        REALM_SC_COMPARE(RemoveIndex, v.object, v.property)
        REALM_SC_COMPARE(RemoveProperty, v.object, v.property)

        #undef REALM_SC_COMPARE
    } visitor{lft};
    return rgt.visit(visitor);
}
} // namespace realm
