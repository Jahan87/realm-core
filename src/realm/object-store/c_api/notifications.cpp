#include <realm/object-store/c_api/types.hpp>
#include <realm/object-store/c_api/util.hpp>

namespace {
struct ObjectNotificationsCallback {
    void* m_userdata;
    realm_free_userdata_func_t m_free = nullptr;
    realm_on_object_change_func_t m_on_change = nullptr;
    realm_callback_error_func_t m_on_error = nullptr;

    ObjectNotificationsCallback() = default;

    ~ObjectNotificationsCallback()
    {
        if (m_free && m_userdata) {
            (m_free)(m_userdata);
        }
    }

    void operator()(const CollectionChangeSet& changes, std::exception_ptr error)
    {
        if (error) {
            if (m_on_error) {
                realm_async_error_t err{std::move(error)};
                m_on_error(m_userdata, &err);
            }
        }
        else if (m_on_change) {
            realm_object_changes_t c{changes};
            m_on_change(m_userdata, &c);
        }
    }
};
} // namespace

RLM_API realm_notification_token_t* realm_object_add_notification_callback(realm_object_t* obj, void* userdata,
                                                                           realm_free_userdata_func_t free,
                                                                           realm_on_object_change_func_t on_change,
                                                                           realm_callback_error_func_t on_error,
                                                                           realm_scheduler_t*)
{
    return wrap_err([&]() {
        ObjectNotificationsCallback cb;
        cb.m_userdata = userdata;
        cb.m_free = free;
        cb.m_on_change = on_change;
        cb.m_on_error = on_error;
        auto token = obj->add_notification_callback(std::move(cb));
        return new realm_notification_token_t{std::move(token)};
    });
}

RLM_API bool realm_object_changes_is_deleted(const realm_object_changes_t* changes)
{
    return !changes->deletions.empty();
}

RLM_API size_t realm_object_changes_get_num_modified_properties(const realm_object_changes_t* changes)
{
    return changes->columns.size();
}

RLM_API size_t realm_object_changes_get_modified_properties(const realm_object_changes_t* changes,
                                                            realm_col_key_t* out_properties, size_t max)
{
    if (!out_properties)
        return changes->columns.size();

    size_t i = 0;
    for (const auto& [col_key_val, index_set] : changes->columns) {
        if (i >= max) {
            break;
        }
        out_properties[i].col_key = col_key_val;
        ++i;
    }
    return i;
}