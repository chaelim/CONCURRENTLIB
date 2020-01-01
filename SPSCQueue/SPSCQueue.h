/****************************************************************************
*
*   SPSCQueue.h
*   Unbounded Single Producer/Single Consumer list-based Queue.
*
*   Originally from http://www.1024cores.net/home/lock-free-algorithms/queues/unbounded-spsc-queue
*   (and http://software.intel.com/en-us/articles/single-producer-single-consumer-queue)
*
*   NOTES:
*      - This is an extremely fast way to implement SPSC compared to other
*        lock-based or RMW implementations.
*      - Unbounded queue can deplete memory when consumer is slower than producer.
*      - Dequeued node's memory is cached and not reclaimed so future enhancement
*        can include a way to reclaim large number of dequeued nodes.
*      - Undefined behavior if more than one producers or consumers exist.
*
***/


#include <atomic>

constexpr int CACHE_LINE_SIZE_BYTES = 64;

/****************************************************************************
*
*   TSPSCQueue
*
***/

template<typename T>
class alignas(CACHE_LINE_SIZE_BYTES) TSPSCQueue
{
private:
    struct Node
    {
        std::atomic<Node*> m_next{nullptr};
        T m_data;

        template<typename U>
        Node(U&& data) : m_data(std::forward<U>(data)) { }

        Node() = default;
    };

    // ======================================================================
    // Consumer part: Accessed mainly by consumer, infrequently by producer
    std::atomic<Node*> m_tail;
    // ======================================================================

    // Padding to make consumer part and producer part never situated in the same cache line.
    // How much it worth? After just adding this padding, test program sped up by 2.5 times.
    // (Tested using Intel i7-4870HQ)
    char m_padding[CACHE_LINE_SIZE_BYTES - sizeof(m_tail)];

    // ======================================================================
    // Producer part: Accessed only by producer
    Node* m_head;
    Node* m_first;        // last unused node (tail of node cache)
    Node* m_tailCopy;     // helper (points somewhere between m_first and m_tail)
    // first -> ... tailCopy ... -> tail -> head
    // ======================================================================

private:
    Node* GetNodeFromCache() noexcept;

public:
    TSPSCQueue();
    ~TSPSCQueue();
    TSPSCQueue(const TSPSCQueue&) = delete;
    TSPSCQueue(const TSPSCQueue&&) = delete;

public:
    inline void Enqueue(const T& data);
    inline void Enqueue(T&& data);
    bool Dequeue(T& data) noexcept;
};

//===========================================================================
// Called from Enqueue (producer thread)
template<typename T>
typename TSPSCQueue<T>::Node* TSPSCQueue<T>::GetNodeFromCache() noexcept
{
    // Try to get node from internal node cache
    Node* node = nullptr;
    for (;;)
    {
        // check if m_first points cached node (already dequeued)
        if (m_first != m_tailCopy)
        {
            node = m_first;
            m_first = m_first->m_next.load(std::memory_order_relaxed);
            break;
        }

        // load tailwith 'consume' (data-dependent) memory ordering
        m_tailCopy = m_tail.load(std::memory_order_consume);
        // check if dequeued node exists
        if (m_first != m_tailCopy)
        {
            node = m_first;
            m_first = m_first->m_next.load(std::memory_order_relaxed);
            break;
        }

        // cached node not found
        break;
    }

    return node;
}

//===========================================================================
template<typename T>
TSPSCQueue<T>::TSPSCQueue()
{
    // Create a dummy node. head, tail initially points to the dummy node
    Node* node = new Node();
    m_tail = m_head = m_first = m_tailCopy = node;
}

//===========================================================================
template<typename T>
TSPSCQueue<T>::~TSPSCQueue()
{
    Node* cur = m_first;
    do
    {
        Node* next = cur->m_next.load(std::memory_order_relaxed);
        delete cur;
        cur = next;
    } while (cur != nullptr);
}

//===========================================================================
template<typename T>
inline void TSPSCQueue<T>::Enqueue(const T& data)
{
    Node* node = GetNodeFromCache();
    if (node != nullptr)
    {
        node->m_next = nullptr;
        node->m_data = data;
    }
    else
    {
        node = new Node(data);
    }

    // "Synchronizes-with" load-consume.
    m_head->m_next.store(node, std::memory_order_release);
    m_head = node;
}

//===========================================================================
template<typename T>
inline void TSPSCQueue<T>::Enqueue(T&& data)
{
    Node* node = GetNodeFromCache();
    if (node != nullptr)
    {
        node->m_next = nullptr;
        node->m_data = std::move(data);
    }
    else
    {
        node = new Node(data);
    }

    // "Synchronizes-with" load-consume.
    m_head->m_next.store(node, std::memory_order_release);
    m_head = node;
}

//===========================================================================
template<typename T>
inline bool TSPSCQueue<T>::Dequeue(T& data) noexcept
{
    // return 'false' if queue is empty
    Node* tail = m_tail.load(std::memory_order_relaxed);
    Node* next = tail->m_next.load(std::memory_order_consume);
    if (next != nullptr)
    {
         data = std::move(next->m_data);
         m_tail.store(next, std::memory_order_release);
         return true;
    }

    return false;
}

