/*************************************************************************
 *
 * Copyright 2018 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#ifndef REALM_BPLUSTREE_HPP
#define REALM_BPLUSTREE_HPP

#include <realm/column_type_traits.hpp>
#include <iostream>

namespace realm {

class BPlusTreeBase;
class BPlusTreeInner;

/*****************************************************************************/
/* BPlusTreeNode                                                             */
/* Base class for all nodes in the BPlusTree. Provides an abstract interface */
/* that can be used by the BPlusTreeBase class to manipulate the tree.       */
/*****************************************************************************/
class BPlusTreeNode {
public:
    struct State {
        int64_t split_offset;
        size_t split_size;
    };

    // Insert an element at 'insert_pos'. May cause node to be split
    using InsertFunc = std::function<size_t(BPlusTreeNode*, size_t insert_pos)>;
    // Access element at 'ndx'. Insertion/deletion not allowed
    using AccessFunc = std::function<void(BPlusTreeNode*, size_t ndx)>;
    // Erase element at erase_pos. May cause nodes to be merged
    using EraseFunc = std::function<size_t(BPlusTreeNode*, size_t erase_pos)>;
    // Function to be called for all leaves in the tree until the function
    // returns 'true'. 'offset' gives index of the first element in the leaf.
    using TraverseFunc = std::function<bool(BPlusTreeNode*, size_t offset)>;

    BPlusTreeNode(BPlusTreeBase* tree)
        : m_tree(tree)
    {
    }

    void change_owner(BPlusTreeBase* tree)
    {
        m_tree = tree;
    }

    virtual ~BPlusTreeNode();

    virtual bool is_leaf() const = 0;
    virtual bool is_compact() const = 0;
    virtual ref_type get_ref() const = 0;

    virtual void init_from_ref(ref_type ref) noexcept = 0;

    virtual void set_parent(ArrayParent* parent, size_t ndx_in_parent) = 0;
    virtual void update_parent() = 0;

    // Number of elements in this node
    virtual size_t get_node_size() const = 0;
    // Size of subtree
    virtual size_t get_tree_size() const = 0;

    virtual ref_type bptree_insert(size_t n, State& state, InsertFunc&) = 0;
    virtual void bptree_access(size_t n, AccessFunc&) = 0;
    virtual size_t bptree_erase(size_t n, EraseFunc&) = 0;
    virtual bool bptree_traverse(TraverseFunc&) = 0;

    // Move elements over in new node, starting with element at position 'ndx'.
    // If this is an inner node, the index offsets should be adjusted with 'adj'
    virtual void move(BPlusTreeNode* new_node, size_t ndx, int64_t offset_adj) = 0;

protected:
    BPlusTreeBase* m_tree;
};

/*****************************************************************************/
/* BPlusTreeLeaf                                                             */
/* Base class for all leaf nodes.                                            */
/*****************************************************************************/
class BPlusTreeLeaf : public BPlusTreeNode {
public:
    using BPlusTreeNode::BPlusTreeNode;

    bool is_leaf() const override
    {
        return true;
    }

    bool is_compact() const override
    {
        return true;
    }

    ref_type bptree_insert(size_t n, State& state, InsertFunc&) override;
    void bptree_access(size_t n, AccessFunc&) override;
    size_t bptree_erase(size_t n, EraseFunc&) override;
    bool bptree_traverse(TraverseFunc&) override;
};

/*****************************************************************************/
/* BPlusTreeBase                                                             */
/* Base class for the actual tree classes.                                   */
/*****************************************************************************/
class BPlusTreeBase {
public:
    BPlusTreeBase(Allocator& alloc)
        : m_alloc(alloc)
    {
        invalidate_leaf_cache();
    }
    virtual ~BPlusTreeBase();

    BPlusTreeBase& operator=(const BPlusTreeBase& rhs);
    BPlusTreeBase& operator=(BPlusTreeBase&& rhs);

    Allocator& get_alloc() const
    {
        return m_alloc;
    }

    bool is_attached() const
    {
        return bool(m_root);
    }

    size_t size() const
    {
        return m_size;
    }

    ref_type get_ref() const
    {
        REALM_ASSERT(is_attached());
        return m_root->get_ref();
    }

    void init_from_ref(ref_type ref)
    {
        auto new_root = create_root_from_ref(ref);
        new_root->set_parent(m_parent, m_ndx_in_parent);

        m_root = std::move(new_root);

        invalidate_leaf_cache();
        m_size = m_root->get_tree_size();
    }

    bool init_from_parent()
    {
        ref_type ref = m_parent->get_child_ref(m_ndx_in_parent);
        if (!ref) {
            return false;
        }
        auto new_root = create_root_from_ref(ref);
        new_root->set_parent(m_parent, m_ndx_in_parent);
        m_root = std::move(new_root);
        invalidate_leaf_cache();
        m_size = m_root->get_tree_size();
        return true;
    }

    void set_parent(ArrayParent* parent, size_t ndx_in_parent)
    {
        m_parent = parent;
        m_ndx_in_parent = ndx_in_parent;
        if (is_attached())
            m_root->set_parent(parent, ndx_in_parent);
    }

    void create();
    void destroy();
    void verify()
    {
    }

protected:
    template <class U>
    struct LeafTypeTrait {
        using type = typename ColumnTypeTraits<U>::cluster_leaf_type;
    };

    friend class BPlusTreeInner;
    friend class BPlusTreeLeaf;

    std::unique_ptr<BPlusTreeNode> m_root;
    Allocator& m_alloc;
    ArrayParent* m_parent = nullptr;
    size_t m_ndx_in_parent = 0;
    size_t m_size = 0;
    size_t m_cached_leaf_begin;
    size_t m_cached_leaf_end;

    void set_leaf_bounds(size_t b, size_t e)
    {
        m_cached_leaf_begin = b;
        m_cached_leaf_end = e;
    }

    void invalidate_leaf_cache()
    {
        m_cached_leaf_begin = size_t(-1);
        m_cached_leaf_end = size_t(-1);
    }

    void adjust_leaf_bounds(int incr)
    {
        m_cached_leaf_end += incr;
    }

    void bptree_insert(size_t n, BPlusTreeNode::InsertFunc& func);
    void bptree_erase(size_t n, BPlusTreeNode::EraseFunc& func);

    // Create an un-attached leaf node
    virtual std::unique_ptr<BPlusTreeLeaf> create_leaf_node() = 0;
    // Create a leaf node and initialize it with 'ref'
    virtual std::unique_ptr<BPlusTreeLeaf> init_leaf_node(ref_type ref) = 0;

    // Initialize the leaf cache with 'mem'
    virtual BPlusTreeLeaf* cache_leaf(MemRef mem) = 0;
    void replace_root(std::unique_ptr<BPlusTreeNode> new_root);
    std::unique_ptr<BPlusTreeNode> create_root_from_ref(ref_type ref);
};

template <>
struct BPlusTreeBase::LeafTypeTrait<ObjKey> {
    using type = ArrayKeyNonNullable;
};

/*****************************************************************************/
/* BPlusTree                                                                 */
/* Actual implementation of the BPlusTree to hold elements of type T.        */
/*****************************************************************************/
template <class T>
class BPlusTree : public BPlusTreeBase {
public:
    BPlusTree(Allocator& alloc)
        : BPlusTreeBase(alloc)
        , m_leaf_cache(this)
    {
    }

    BPlusTree(const BPlusTree& other)
        : BPlusTree(other.get_alloc())
    {
        *this = other;
    }

    BPlusTree(BPlusTree&& other)
        : BPlusTree(other.get_alloc())
    {
        *this = std::move(other);
    }

    /********************* Assignment ********************/

    BPlusTree& operator=(const BPlusTree& rhs)
    {
        this->BPlusTreeBase::operator=(rhs);
        return *this;
    }

    BPlusTree& operator=(BPlusTree&& rhs)
    {
        this->BPlusTreeBase::operator=(std::move(rhs));
        return *this;
    }

    /************ Tree manipulation functions ************/

    static T default_value()
    {
        return LeafArray::default_value(false);
    }

    void add(T value)
    {
        insert(npos, value);
    }

    void insert(size_t n, T value)
    {
        BPlusTreeNode::InsertFunc func = [value](BPlusTreeNode* node, size_t ndx) {
            LeafNode* leaf = static_cast<LeafNode*>(node);
            leaf->LeafArray::insert(ndx, value);
            return leaf->size();
        };

        bptree_insert(n, func);
        m_size++;
    }

    T get(size_t n) const
    {
        if (m_cached_leaf_begin <= n && n < m_cached_leaf_end) {
            return m_leaf_cache.get(n - m_cached_leaf_begin);
        }
        else {
            T value;

            BPlusTreeNode::AccessFunc func = [&value](BPlusTreeNode* node, size_t ndx) {
                LeafNode* leaf = static_cast<LeafNode*>(node);
                value = leaf->get(ndx);
            };

            m_root->bptree_access(n, func);

            return value;
        }
    }

    std::vector<T> get_all() const
    {
        std::vector<T> all_values;
        all_values.reserve(m_size);

        BPlusTreeNode::TraverseFunc func = [&all_values](BPlusTreeNode* node, size_t) {
            LeafNode* leaf = static_cast<LeafNode*>(node);
            size_t sz = leaf->size();
            for (size_t i = 0; i < sz; i++) {
                all_values.push_back(leaf->get(i));
            }
            return false;
        };

        m_root->bptree_traverse(func);

        return all_values;
    }

    void set(size_t n, T value)
    {
        BPlusTreeNode::AccessFunc func = [value](BPlusTreeNode* node, size_t ndx) {
            LeafNode* leaf = static_cast<LeafNode*>(node);
            leaf->set(ndx, value);
        };

        m_root->bptree_access(n, func);
    }

    void erase(size_t n)
    {
        BPlusTreeNode::EraseFunc func = [](BPlusTreeNode* node, size_t ndx) {
            LeafNode* leaf = static_cast<LeafNode*>(node);
            leaf->LeafArray::erase(ndx);
            return leaf->size();
        };

        bptree_erase(n, func);
        m_size--;
    }

    void clear()
    {
        if (m_root->is_leaf()) {
            LeafNode* leaf = static_cast<LeafNode*>(m_root.get());
            leaf->truncate_and_destroy_children(0);
        }
        else {
            destroy();
            create();
            if (m_parent) {
                m_parent->update_child_ref(m_ndx_in_parent, get_ref());
            }
        }
        m_size = 0;
    }

    size_t find_first(T value) const noexcept
    {
        size_t result = realm::npos;

        BPlusTreeNode::TraverseFunc func = [&result, value](BPlusTreeNode* node, size_t offset) {
            LeafNode* leaf = static_cast<LeafNode*>(node);
            size_t sz = leaf->size();
            auto i = leaf->find_first(value, 0, sz);
            if (i < sz) {
                result = i + offset;
                return true;
            }
            return false;
        };

        m_root->bptree_traverse(func);

        return result;
    }

    void dump_values() const
    {
        BPlusTreeNode::TraverseFunc func = [](BPlusTreeNode* node, size_t offset) {
            LeafNode* leaf = static_cast<LeafNode*>(node);
            size_t sz = leaf->size();
            std::cout << "Offset: " << offset << std::endl;
            for (size_t i = 0; i < sz; i++) {
                std::cout << "  " << leaf->get(i) << std::endl;
            }
            return false;
        };

        m_root->bptree_traverse(func);
    }

protected:
    using LeafArray = typename LeafTypeTrait<T>::type;

    /**
     * Actual class for the leaves. Maps the abstract interface defined
     * in BPlusTreeNode onto the specific array class
     **/
    class LeafNode : public BPlusTreeLeaf, public LeafArray {
    public:
        LeafNode(BPlusTreeBase* tree)
            : BPlusTreeLeaf(tree)
            , LeafArray(tree->get_alloc())
        {
        }

        void init_from_ref(ref_type ref) noexcept override
        {
            LeafArray::init_from_ref(ref);
        }

        ref_type get_ref() const override
        {
            return LeafArray::get_ref();
        }

        void set_parent(realm::ArrayParent* p, size_t n) override
        {
            LeafArray::set_parent(p, n);
        }

        void update_parent() override
        {
            LeafArray::update_parent();
        }

        size_t get_node_size() const override
        {
            return LeafArray::size();
        }

        size_t get_tree_size() const override
        {
            return LeafArray::size();
        }

        void move(BPlusTreeNode* new_node, size_t ndx, int64_t) override
        {
            LeafNode* dst(static_cast<LeafNode*>(new_node));
            size_t end = get_node_size();

            for (size_t j = ndx; j < end; j++) {
                dst->add(LeafArray::get(j));
            }
            LeafArray::truncate_and_destroy_children(ndx);
        }
    };

    LeafNode m_leaf_cache;

    /******** Implementation of abstract interface *******/

    std::unique_ptr<BPlusTreeLeaf> create_leaf_node() override
    {
        std::unique_ptr<BPlusTreeLeaf> leaf = std::make_unique<LeafNode>(this);
        static_cast<LeafNode*>(leaf.get())->create();
        return leaf;
    }
    std::unique_ptr<BPlusTreeLeaf> init_leaf_node(ref_type ref) override
    {
        std::unique_ptr<BPlusTreeLeaf> leaf = std::make_unique<LeafNode>(this);
        leaf->init_from_ref(ref);
        return leaf;
    }
    BPlusTreeLeaf* cache_leaf(MemRef mem) override
    {
        m_leaf_cache.init_from_mem(mem);
        return &m_leaf_cache;
    }
};
}

#endif /* REALM_BPLUSTREE_HPP */
