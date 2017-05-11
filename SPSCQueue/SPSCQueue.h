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
*        lock-based or RMW implementations
*      - Unbounded queue can deplete memory when consumer is slower than producer
*      - Dequeued node's memory is cached and not reclaimed so future enhancement
*        can include a way to reclaim large number of dequeued nodes
*
***/


#include <atomic>

const int CACHE_LINE_SIZE_BYTES = 64;

/****************************************************************************
*
* TSPSCQueue
*
***/

template<typename T>
class TSPSCQueue
{
private:
    struct Node
    {
        std::atomic<Node *> m_next = { nullptr };
        T m_data;
        
        template<typename U>
        Node(U&& data)
            : m_data(std::forward<U>(data)) { }
        
        Node() = default;
    };

    // Consumer part: Accessed mainly by consumer, infrequently be producer
    std::atomic<Node *> m_tail;

    // Padding to make consumer part and producer part never situated in the same cache line.
    // How much it worth? After just adding this padding test program speeded up by 2.5 times.
    // (Tested using Intel i7-4870HQ)
    char m_padding[CACHE_LINE_SIZE_BYTES - sizeof(Node *)];

    // Producer part: Accessed only by producer
    Node* m_head;
    Node* m_first;        // last unused node (tail of node cache)
    Node* m_tailCopy;     // helper (points somewhere between m_first and m_tail)
    
    // first -> ... tailCopy ... -> tail -> head

private:
    Node* GetNode();

public:
    TSPSCQueue ();
    ~TSPSCQueue ();

public:
    inline void Enqueue(const T& data);
    inline void Enqueue(T&& data);
    bool Dequeue(T& data);
};

//===========================================================================
template<typename T>
typename TSPSCQueue<T>::Node* TSPSCQueue<T>::GetNode()
{
    // Tries to get node from internal node cache
    Node* node = nullptr;
    for (;;) {
        // check if m_first points cached node (already dequeued)
        if (m_first != m_tailCopy) {
            node = m_first;
            m_first = m_first->m_next;
            break;
        }

        // load with 'consume' (data-dependent) memory ordering
        m_tailCopy = m_tail.load(std::memory_order_consume);
        if (m_first != m_tailCopy) {
            node = m_first;
            m_first = m_first->m_next;
            break;
        }

        // cached node not found
        break;
    }

    return node;
}

//===========================================================================
template<typename T>
TSPSCQueue<T>::TSPSCQueue ()
{
    // Create dummy node and head, tail initially points to the dummy node
    auto node = new Node();
    memset(node, 0, sizeof(Node));
    m_tail = m_head = m_first = m_tailCopy = node;
}

//===========================================================================
template<typename T>
TSPSCQueue<T>::~TSPSCQueue()
{
    auto cur = m_first;
    do { 
        auto next = cur->m_next.load(std::memory_order_relaxed);
        delete cur;
        cur = next;
    } while (cur);
}

//===========================================================================
template<typename T>
void TSPSCQueue<T>::Enqueue(const T& data)
{
    auto node = GetNode();
    if (node != nullptr) {
        node->m_next = nullptr;
        node->m_data = data;
    }
    else {
        node = new Node(data);
    }

    // "Synchronizes-with" load-consume.
    m_head->m_next.store(node, std::memory_order_release);
    m_head = node;
}

//===========================================================================
template<typename T>
void TSPSCQueue<T>::Enqueue(T&& data)
{
    auto node = GetNode();
    if (node != nullptr) {
        node->m_next = nullptr;
        node->m_data = std::move(data);
    }
    else {
        node = new Node(data);
    }

    // "Synchronizes-with"  load-consume.
    m_head->m_next.store(node, std::memory_order_release);
    m_head = node;
}

//===========================================================================
template<typename T>
bool TSPSCQueue<T>::Dequeue(T& data)
{
    // returns 'false' if queue is empty
    auto tail = m_tail.load(std::memory_order_relaxed);
    auto next = tail->m_next.load(std::memory_order_consume);
    if (next != nullptr) {
         data = std::move(next->m_data);
         m_tail.store(next, std::memory_order_release);
         return true;
    }

    return false;
}
