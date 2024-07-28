

#include <iostream>
#include <cassert>
#include <time.h>
#include <vector>

#include "MemoryPool.h"
#include "myStack.h"

/* Adjust these values depending on how much you trust your computer */
#define ELEMS 1000000
#define REPS 50

int main()
{
  clock_t start;

  {
    // 使用默认的内存分配器模拟一个链式栈 栈元素类型为int
    myStack<int, std::allocator<int> > stackDefault;
    start = clock();
    for (int j = 0; j < REPS; j++)
    {
      assert(stackDefault.empty());
      for (int i = 0; i < ELEMS / 4; i++) {
        // Unroll to time the actual code and not the loop
        stackDefault.push(i);
        stackDefault.push(i);
        stackDefault.push(i);
        stackDefault.push(i);
      }

      for (int i = 0; i < ELEMS / 4; i++) {
        // Unroll to time the actual code and not the loop
        stackDefault.pop();
        stackDefault.pop();
        stackDefault.pop();
        stackDefault.pop();
      }
    }

    std::cout << "Default Allocator Time: ";
    std::cout << (((double)clock() - start) / CLOCKS_PER_SEC) << "\n\n";
  }     //1.97s



  {
    // 使用内存池模拟一个链式栈
    myStack<int, MemoryPool<int> > stackPool;
    start = clock();
    for (int j = 0; j < REPS; j++)
    {
      assert(stackPool.empty());
      for (int i = 0; i < ELEMS / 4; i++) {
        // Unroll to time the actual code and not the loop
        stackPool.push(i);
        stackPool.push(i);
        stackPool.push(i);
        stackPool.push(i);
      }

      for (int i = 0; i < ELEMS / 4; i++) {
        // Unroll to time the actual code and not the loop
        stackPool.pop();
        stackPool.pop();
        stackPool.pop();
        stackPool.pop();
      }
    }
    std::cout << "MemoryPool Allocator Time: ";           
    std::cout << (((double)clock() - start) / CLOCKS_PER_SEC) << "\n\n";
    //1.12s

  }

  std::cout << "最高效的方式其实是使用vector来模拟栈\n";

  /* Compare MemoryPool to std::vector */
  std::vector<int> stackVector;
  start = clock();
  for (int j = 0; j < REPS; j++)
  {
    assert(stackVector.empty());
    for (int i = 0; i < ELEMS / 4; i++) {
      // Unroll to time the actual code and not the loop
      stackVector.push_back(i);
      stackVector.push_back(i);
      stackVector.push_back(i);
      stackVector.push_back(i);
    }

    for (int i = 0; i < ELEMS / 4; i++) {
      // Unroll to time the actual code and not the loop
      stackVector.pop_back();
      stackVector.pop_back();
      stackVector.pop_back();
      stackVector.pop_back();
    }
  }

  std::cout << "Vector Time: ";
  std::cout << (((double)clock() - start) / CLOCKS_PER_SEC) << "\n\n";
  //0.99s

  std::cout << "结果证明，使用vector更快.\n\n";
  
  std::cout << "MemoryPool 仍然在很多情况下非常有用，特别是在处理特定类型的数据结构时，如树结构或当你有多个链表时。这些场景中的每个数据结构都可以共享同一个内存池，从而带来显著的效率和性能优势"<<std::endl;;

  return 0;
}


//g++ *.cpp -o test
/*
Default Allocator Time: 2.27

MemoryPool Allocator Time: 1.18

Here is a secret: the best way of implementing a stack is a dynamic array.
Vector Time: 1.51

The vector implementation will probably be faster.

MemoryPool still has a lot of uses though. Any type of tree and when you have multiple linked lists are some examples (they can all share the same memory pool).
*/