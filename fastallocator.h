#pragma once

#include <iostream>
#include <vector>
#include <type_traits>
#include <list>
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <stdexcept>

const size_t BUBEN = 32;

template<size_t chunkSize>
class FixedAllocator {
private:

    struct Node {
        uint8_t memory[chunkSize - 8];
        Node* go = nullptr;
    };

    Node* cur = nullptr;

    std::vector<Node*> forDelete;

    Node* getNewBlock() {
        Node* ptr = new Node[BUBEN];
        for (size_t i = 0; i < BUBEN - 1; i++)
            ptr[i].go = ptr + i + 1;

        forDelete.emplace_back(ptr);
        return ptr;
    }

    FixedAllocator() {}

public:

    void* allocate() {
        if (!cur)
            cur = getNewBlock();
        void* ans = cur->memory;
        cur = cur->go;
        return ans;
    }

    void deallocate(void* ptr) {
        Node* tmp = static_cast<Node*>(ptr);
        tmp->go = cur;
        cur = tmp;
    }

    virtual ~FixedAllocator() {
        for (auto u : forDelete) {
            delete[] u;
        }
    }

    static FixedAllocator& getInstance() {
        static FixedAllocator alloc;
        return alloc;
    }
};

constexpr size_t MAX_FIXED_ALLOCATOR_CHUNK_SIZE = 32;

template<typename T>
class FastAllocator {
public:
    using value_type = T;

    T* allocate(size_t n) {
        if (n * sizeof(T) > MAX_FIXED_ALLOCATOR_CHUNK_SIZE)
            return std::allocator<T>().allocate(n);

        if (n * sizeof(T) <= 8)
            return static_cast<T*>(FixedAllocator<8>::getInstance().allocate());
        if (n * sizeof(T) <= 16)
            return static_cast<T*>(FixedAllocator<16>::getInstance().allocate());
        
        return static_cast<T*>(FixedAllocator<32>::getInstance().allocate());
    }

    void deallocate(T* ptr, size_t n) {
        if (n * sizeof(T) > MAX_FIXED_ALLOCATOR_CHUNK_SIZE) {
            std::allocator<T>().deallocate(ptr, n);
            return;
        }
        
        if (n * sizeof(T) <= 8) {
            FixedAllocator<8>::getInstance().deallocate(ptr);
            return;
        }
        if (n * sizeof(T) <= 16) {
            FixedAllocator<16>::getInstance().deallocate(ptr);
            return;
        }
        FixedAllocator<32>::getInstance().deallocate(ptr);
    }

    FastAllocator() {}

    template <typename U>
    FastAllocator(const FastAllocator<U>&) {}

    virtual ~FastAllocator() {}

    template<typename U>
    bool operator != (const FastAllocator<U>&) const {
        return true;
    }

    template<typename U>
    bool operator == (const FastAllocator<U>& other) const {
        return !(*this != other);
    }

    template<typename U>
    FastAllocator& operator = (const FastAllocator<U>&) {
        return *this;
    }
};

template<typename T, typename Allocator = std::allocator<T>>
class List {
private:

    struct Node {
        T value;

        Node* left = nullptr;
        Node* right = nullptr;

        Node(Node* left = nullptr, Node* right = nullptr) : left(left), right(right) {}

        Node(const T& val, Node* left = nullptr, Node* right = nullptr)
            : value(val), left(left), right(right) {}

    };

    using NodeAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;
    using NodeAllocTraits = typename std::allocator_traits<NodeAlloc>;

    Node* root;
    size_t sz;

    NodeAlloc allocN;
    Allocator allocT;

    void putAfter(Node* ptr, const T& value) {

        Node* tmp = NodeAllocTraits::allocate(allocN, 1);
        NodeAllocTraits::construct(allocN, tmp, value, ptr, ptr->right);

        ptr->right = tmp;
        tmp->right->left = tmp;

        sz++;
    }

    void putAfter(Node* ptr, Node* value) {
        value->left = ptr;
        value->right = ptr->right;

        ptr->right = value;
        value->right->left = value;

        sz++;
    }

    void del(Node* ptr) {
        assert(ptr != root);

        ptr->left->right = ptr->right;
        ptr->right->left = ptr->left;

        NodeAllocTraits::destroy(allocN, ptr);
        NodeAllocTraits::deallocate(allocN, ptr, 1);

        sz--;
    }

    void genList(size_t sz) {
        this->sz = sz;

        root = NodeAllocTraits::allocate(allocN, 1);
        root->right = root->left = root;

        if (sz == 0) return;

        Node* left = root;
        for (size_t i = 0; i < sz; i++) {
            Node* tmp = NodeAllocTraits::allocate(allocN, 1);

            left->right = tmp;
            tmp->left = left;

            root->left = tmp;
            tmp->right = root;

            left = tmp;
        }
    }

    void destroy() {
        root = root->right;
        for (size_t i = 0; i < sz; i++) {
            Node* tmp = root;
            root = root->right;
            NodeAllocTraits::destroy(allocN, tmp);
            NodeAllocTraits::deallocate(allocN, tmp, 1);
        }

        NodeAllocTraits::deallocate(allocN, root, 1);
        root = nullptr, sz = 0;
    }

public:

    explicit List(const Allocator& alloc = Allocator()) : allocN(alloc) {
        genList(0);
    }

    List(size_t count, const T& value, const Allocator& alloc = Allocator())
        : allocN(alloc) {

        genList(count);

        for (Node* it = root->right; it != root; it = it->right) {
            NodeAllocTraits::construct(allocN, it, value, it->left, it->right);
        }
    }

    List(size_t count, const Allocator& alloc = Allocator())
        : allocN(alloc) {

        genList(count);

        for (Node* it = root->right; it != root; it = it->right) {
            NodeAllocTraits::construct(allocN, it, it->left, it->right);
        }
    }

    List(const List& ls)
        : List(std::allocator_traits<Allocator>::select_on_container_copy_construction(ls.get_allocator())) {

        genList(ls.sz);

        for (Node* cur = root->right, *curLs = ls.root->right;
            cur != root; cur = cur->right, curLs = curLs->right) {

            NodeAllocTraits::construct(allocN, cur, curLs->value, cur->left, cur->right);
        }
    }

    List(List&& other) {
        sz = other.sz;
        root = other.root;
        allocN = std::move(other.allocN);

        other.sz = 0;
        other.root = NodeAllocTraits::allocate(allocN, 1);
        other.root->right = other.root->left = other.root;
    }

    List& operator = (const List& ls) {
        if (this == &ls) return *this;

        destroy();
        if (std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value)
            allocN = ls.allocN;

        genList(ls.sz);

        for (Node* cur = root->right, *curLs = ls.root->right;
            cur != root; cur = cur->right, curLs = curLs->right) {

            NodeAllocTraits::construct(allocN, cur, curLs->value, cur->left, cur->right);
        }
        return *this;
    }

    List& operator = (List&& other) {
        if (this == &other) return *this;

        destroy();

        sz = other.sz;
        root = other.root;
        if (allocN != other.allocN &&
            std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value)

            allocN = std::move(other.allocN);

        other.sz = 0;
        other.root = NodeAllocTraits::allocate(other.allocN, 1);
        other.root->right = other.root->left = other.root;

        return *this;
    }

    Allocator get_allocator() const {
        return allocN;
    }

    size_t size() const {
        return sz;
    }

    void push_back(const T& value) {
        putAfter(root->left, value);
    }

    void push_front(const T& value) {
        putAfter(root, value);
    }

    void pop_back() {
        del(root->left);
    }

    void pop_front() {
        del(root->right);
    }

    template<bool isConst, bool isReversed>
    class BaseIterator {
    private:

        using curT = std::conditional_t<isConst, const T, T>;
        using curTRef = std::conditional_t<isConst, const T&, T&>;
        using curTPtr = std::conditional_t<isConst, const T*, T*>;

        Node* ptr;

    public:

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using reference = T&;
        using pointer = T*;
        using difference_type = std::ptrdiff_t;

        BaseIterator(Node* ptr = nullptr) : ptr(ptr) {}

        template<bool isReversedOther>
        BaseIterator(const BaseIterator<isConst, isReversedOther>& other) : ptr(other.getPtr()) {}

        template<bool isReversedOther>
        operator BaseIterator<true, isReversedOther>() {
            return BaseIterator<true, isReversedOther>(ptr);
        }

        BaseIterator& operator = (const BaseIterator& other) {
            ptr = other.ptr;
            return *this;
        }

        BaseIterator& operator++() {
            if (!isReversed)
                ptr = ptr->right;
            else
                ptr = ptr->left;

            return *this;
        }

        BaseIterator& operator--() {
            if (!isReversed)
                ptr = ptr->left;
            else
                ptr = ptr->right;

            return *this;
        }

        BaseIterator operator++(int) {
            BaseIterator ans = *this;
            ++(*this);
            return ans;
        }

        BaseIterator operator--(int) {
            BaseIterator ans = *this;
            --(*this);
            return ans;
        }

        curTRef operator* () const {
            return ptr->value;
        }

        curTPtr operator-> () const {
            return &(ptr->value);
        }

        template<bool isConstOther, bool isReversedOther>
        bool operator == (const BaseIterator<isConstOther, isReversedOther>& other) {
            return ptr == other.getPtr();
        }

        template<bool isConstOther, bool isReversedOther>
        bool operator != (const BaseIterator<isConstOther, isReversedOther>& other) {
            return ptr != other.getPtr();
        }

        Node* getPtr() const {
            return ptr;
        }

        BaseIterator base() {
            return BaseIterator<isConst, !isReversed>(ptr->right);
        }

    };

    using iterator = BaseIterator<false, false>;
    using const_iterator = BaseIterator<true, false>;
    using reverse_iterator = BaseIterator<false, true>;
    using const_reverse_iterator = BaseIterator<true, true>;

    iterator begin() const {
        return root->right;
    }

    const_iterator cbegin() const {
        return root->right;
    }

    reverse_iterator rbegin() {
        return root->left;
    }

    const_reverse_iterator rbegin() const {
        return root->left;
    }

    const_reverse_iterator crbegin() const {
        return root->left;
    }

    iterator end() {
        return root;
    }

    const_iterator end() const {
        return root;
    }

    const_iterator cend() const {
        return root;

    }
    reverse_iterator rend() {
        return root;
    }

    const_reverse_iterator rend() const {
        return root;
    }

    const_reverse_iterator crend() const {
        return root;
    }

    void insert(const_iterator it, const T& value) {
        putAfter(it.getPtr()->left, value);
    }

    void erase(const_iterator it) {
        del(it.getPtr());
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        Node* tmp = NodeAllocTraits::allocate(allocN, 1);
        std::allocator_traits<Allocator>::construct(allocT, &tmp->value, std::forward<Args>(args)...);
        putAfter(root->left, tmp);
    }

    ~List() {
        destroy();
    }

};