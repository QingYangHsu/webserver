
#include<atomic>
#include<iostream>
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>
using namespace std;
 
template<class T>				//节点value的类型
class LockFreeQueue				//无锁队列
{
    struct Node
    {
        T _value;
        atomic<Node*> _next;
        Node(const T& v)
            :_value(v)
            ,_next(nullptr)
        {}
    };
public:
    LockFreeQueue()
    {
        _fakehead = _tail = new Node(T());
    }
 
    ~LockFreeQueue()
    {
        Node* cur = _fakehead;
        while(cur)
        {
            Node* next = cur->_next;
            delete cur;
            cur = next;
        }
    }
    
    void Enqueue(const T& x)
	{
		Node* newnode = new Node(x);	//要插入的新值
		Node* oldtail = nullptr;		//旧的尾节点
		Node* nullnode = nullptr;		//空节点
		do
		{
			oldtail = _tail.load();		//用load取出当前_tail节点的值 因为_tail在多线程环境下随时可能改变
		} while (oldtail->_next.compare_exchange_weak(nullnode, newnode) != true);
		//如果当前tail节点的下一个节点是空，也就是等于参数一，那么修改为参数二
		
		//这里是有问题的，如果线程执行到这里挂了，那么别的线程也执行不了上面的语句，就会产生死锁
		//由于现在的真正的尾节点是newnode，所以将_tail节点更新为newnode
		_tail.compare_exchange_weak(oldtail, newnode);
	}
	
	T DeQueue() {							//返回值为出队节点的value
		Node* oldfakehead = _fakehead.load();		//取出当前的头节点，也就是伪节点
		T ret;
		do
		{
			Node* nextnode = oldfakehead->_next;		//取出头节点的下一个节点，此节点是预期的出队节点
			if(nextnode == nullptr)
			{
				return T();
			}
			else
			{
				ret = nextnode->_value;				//取走值
			}
		} while(_fakehead.compare_exchange_weak(oldfakehead,oldfakehead->_next) != true);	
		//将伪头节点修改为下一个节点
		
		//和上面问题一样，如果线程执行到这里挂掉了，又是死锁情况
		//删除旧的伪头结点
		delete oldfakehead;
		return ret;
	}
	
	bool Empty()
	{
		return _fakehead.load() == _tail.load();
	}
	
	LockFreeQueue(const LockFreeQueue<T>&) = delete;
	
private:
    atomic<Node*> _fakehead;
    atomic<Node*> _tail;
};

mutex mutx;

int main()
{
	LockFreeQueue<int> lq;
	queue<int> nq;

	//针对atomic类型的变量赋值不允许采用等号
	atomic<size_t> n(100000);

	const int threadcount = 2;
 
	//测试无锁队列
	{
		size_t costtime = 0;
		vector<thread> threads;
		for (size_t i = 0; i < threadcount; ++i)
		{
			threads.push_back(thread([&]()
			{
				//线程主函数
				size_t begin = 0, end = 0;
				begin = clock();
				for (size_t j = 0; j < n; ++j)
				{
					lq.Enqueue(j);
				}
				end = clock();
				costtime += (end - begin);
			}));
		}
		//主线程回收子线程资源
		for (auto& e : threads)
		{
			e.join();
		}
		cout << "无锁队列时间"<<costtime << endl;
	}
 
	//测试有锁队列
	{
		size_t costtime = 0;
		vector<thread> threads;
		for (size_t i = 0; i < threadcount; ++i)
		{
			threads.push_back(thread([&]()
			{
				size_t begin = 0, end = 0;
				begin = clock();
				for (size_t j = 0; j < n; ++j)
				{
					mutx.lock();
					nq.push(j);
					mutx.unlock();
				}
				end = clock();
				costtime += (end - begin);
			}));
		}
		for (auto& e : threads)
		{
			e.join();
		}
		cout << "有锁队列时间"<<costtime << endl;
	}
 
	return 0;
}

//g++ lock_free_queue.cpp -o test1 -std=c++11 -lpthread

/*
load函数：
功能：从原子对象中加载（读取）当前的值。
参数：memory_order sync是一个可选参数，默认值为memory_order_seq_cst，表示这是一个顺序一致性的内存操作。内存顺序决定了操作在内存中的可见性和顺序。
返回值：返回原子对象当前的值。
特性：const volatile noexcept表示这个函数不会修改对象的状态（const），可以应用于易变对象（volatile），并且保证不会抛出异常（noexcept）。

compare_exchange_weak函数：
功能：如果原子对象中当前的值等于expected参数的值，则将原子对象的值替换为val参数的值，并返回true；否则，将expected参数更新为原子对象中当前的值，并返回false。这是一个“比较并交换”操作，通常用于实现锁和其他同步机制。
参数：
T& expected：一个引用参数，用于传入期望的值，并在操作失败时更新为原子对象中当前的值。
T val：如果比较成功，原子对象将被设置为这个值。
memory_order sync：一个可选参数，默认值为memory_order_seq_cst，表示这是一个顺序一致性的内存操作。
返回值：如果比较成功（即原子对象中的值等于expected），则返回true；否则返回false。
特性：volatile noexcept表示这个函数可以应用于易变对象（volatile），并且保证不会抛出异常（noexcept）。
*/


//虽然实现了无锁队列，但是无锁队列并不是万能的，我们在无锁队列中使用的atoic族中的load 与 cas操作，
//cas操作本质上属于一种乐观锁，即认为当前全局并发情况没有那么严重,因此让cas失败的线程一直while等待，属于是忙等待，浪费了cpu资源
//而mutex是一种悲观锁，认为系统并发情况很严重，所以要使用锁
//所以如果系统线程数较多，并发情况严重，cas的情况可能并不如互斥锁高效
//并且cas存在aba问题，解决方式是线程进行cas操作时对数据带一个版本号，以标定对数据的操作版本