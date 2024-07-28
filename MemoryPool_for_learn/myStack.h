
#ifndef MY_STACK_H
#define MY_STACK_H

#include <memory>

template <typename T>

//栈节点
struct StackNode_
{
  T data;
  StackNode_* prev;
};


//T是栈节点中的data的类型
//默认使用std的内存分配器
template <class T, class Alloc = std::allocator<T> >
class myStack
{
  public:
    typedef StackNode_<T> Node;
    typedef typename Alloc::template rebind<Node>::other allocator;

    myStack() {
      head_ = 0; 
    }

    ~myStack() { 
      clear(); 
    }

    bool empty() {
      return (head_ == 0);
    }

    void clear() {
      Node* curr = head_;

      //从栈顶到栈底逐个析构对象，回收空间
      while (curr != 0)
      {
        Node* tmp = curr->prev;
        //析构对象
        allocator_.destroy(curr);
        
        //回收空间
        allocator_.deallocate(curr, 1);
        curr = tmp;
      }

      head_ = 0;
    }

    void push(T element) {

      //申请一个元素的空间
      Node* newNode = allocator_.allocate(1);
      
      //在刚申请的空间上构造对象
      allocator_.construct(newNode, Node());
      
      newNode->data = element;
      newNode->prev = head_;
      head_ = newNode;
    }

    T pop() {
      T result = head_->data;
      Node* tmp = head_->prev;
      //析构对象
      allocator_.destroy(head_);
      //回收空间
      allocator_.deallocate(head_, 1);
      //栈顶指针移动
      head_ = tmp;
      return result;
    }

    /** Return the topmost element */
    T top() { 
      return (head_->data); 
    }

  private:
    allocator allocator_;
    Node* head_;      //栈顶指针
};

#endif // MY_STACK_H
