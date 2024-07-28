#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <iostream> 
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <mutex>
#include <fstream>
#include"../memorypool/memorypool.h"

//落盘路径
//#define STORE_FILE "store/dumpFile"
#define STORE_FILE "USER.txt"

const std::string delimiter = ":";            //分隔符 用来分割k与v

//跳表查找任意数据的时间复杂度为O(logn)
//Redis中的有序集合是通过跳表来实现的，当然还用到了散列表。

//模板类的定义与实现必须都在同一个文件 否则会出现编译问题 即使加了预处理编译命令也没用
template<typename K, typename V> 
class skiplist_Node {
public:
    
    skiplist_Node() {} 

    skiplist_Node(K k, V v, int); 

    ~skiplist_Node();

    K get_key() const;

    V get_value() const;

    void set_value(V);
    
    //node** forward相当于一维node*数组的首地址，在构造函数中，该一维node*数组全都初始化为NULL
    //level也代表了forward数组的size
    //forward相当于链表节点的next指针 只不过这里要存很多个指针 所以用数组来存
    skiplist_Node<K, V> **forward;

    int node_level;

private:
    K key;
    V value;
};

template<typename K, typename V> 
skiplist_Node<K, V>::skiplist_Node(const K k, const V v, int level) {
    this->key = k;
    this->value = v;
    this->node_level = level; 

    this->forward = new skiplist_Node<K, V>* [level+1];
    
	// forward指向的一维node*数组全都初始化为NULL
    memset(this->forward, 0, sizeof(skiplist_Node<K, V>*) * (level+1));
};

template<typename K, typename V> 
skiplist_Node<K, V>::~skiplist_Node() {
    delete []forward;
};

template<typename K, typename V> 
K skiplist_Node<K, V>::get_key() const {
    return key;
};

template<typename K, typename V> 
V skiplist_Node<K, V>::get_value() const {
    return value;
};

template<typename K, typename V> 
void skiplist_Node<K, V>::set_value(V value) {
    this->value = value;
};


template <typename K, typename V> 
class SkipList {

public: 
    static const int DEFAULT_MAX_LEVEL = 16; // 假设的默认值
    
    static SkipList<K,V>& getinst() {   //单例 懒汉 
        static SkipList<K,V> inst(DEFAULT_MAX_LEVEL);
        return inst;
    }               
    
    ~SkipList();
    int get_random_level();                 //构造节点获得一个随机level，不会超过max_level
    skiplist_Node<K, V>* create_node(K, V, int);     //创造一个新节点，调用Node类构造函数
    int insert_element(K, V);               //插入一个element 
    void display_list();                    //level从低到高打印节点
    bool search_element(K, V&);                 //根据key查找element
    void delete_element(K);             
    void delete_skiplist();                 //函数作用，将跳表删除，只剩下头结点
    void dump_file();                       //数据落盘    
    void load_file();                       //读盘数据
    int size();

private:
    SkipList(int);                          //构造函数 参数为maxlevel
    void get_key_value_from_string(const std::string& str, std::string* key, std::string* value);
    bool is_valid_string(const std::string& str);

private:    
    // 这个值限定了每个跳表节点的forward数组的size
    int _max_level;

    // current level of skip list 
    int _cur_level;

    // pointer to fakeheader node 
    skiplist_Node<K, V> *fakehead;

    // file operator文件标识符，可以调用该标识符的成员函数操作指定路径的文件
    std::ofstream _file_writer;//写文件描述符
    std::ifstream _file_reader;//读文件描述符

    // 当前跳表节点个数
    int _element_count;

    // 互斥量 不用做初始化动作，因为其默认ctor已经做了锁的初始化
    std::mutex mtx;                         
};

/* 
                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+

*/
template<typename K, typename V> 
bool SkipList<K, V>::search_element(K key, V& value) {

    std::cout << "search_element-----------------" << std::endl;
    skiplist_Node<K, V> *current = fakehead;//从头结点开始找

    // 从上向下找。从当前节点的最高层级往下找
    for (int i = _cur_level; i >= 0; i--) {
        while (current->forward[i] && current->forward[i]->get_key() < key) {
            current = current->forward[i];
        }
        //while终止条件：两种情况
        //1.current下一个节点为空，表示到达本level末尾，level该下降了,在下一level继续查找
        //2.或者是current下一个节点的值比查找值key大于等于,此时current下一个节点就是可能的结点，现在只需要下降level，while不会再进去，直到level降到0
    }

    //current移动之后，要么找到结点指向它，其值可能大于等于待查值，要么到达跳表尾部，指向NULL
    current = current->forward[0];

    // 如果找到的结点的值正好为待查值
    if (current && current->get_key() == key) {
        value = current->get_value();
        std::cout << "Found key: " << key << ", value: " << current->get_value() << std::endl;
        return true;
    }

    std::cout << "Not Found Key:" << key << std::endl;
    return false;
}


template<typename K, typename V> 
SkipList<K, V>::SkipList(int max_level) {
    init_MemoryPool();

    this->_max_level = max_level;
    this->_cur_level = 0;         
    this->_element_count = 0;           //元素数量 不包括伪头结点

    // create header node and initialize key and value to null
    K k;
    V v;
    //this->fakehead = new skiplist_Node<K, V>(k, v, _max_level);           //造一个假头节点 k与v都是类型默认值
    this->fakehead = newElement<skiplist_Node<K, V>>(k, v, _max_level);
};


template<typename K, typename V>
skiplist_Node<K, V>* SkipList<K, V>::create_node(const K k, const V v, int level) {
    //skiplist_Node<K, V> *n = new skiplist_Node<K, V>(k, v, level);
    skiplist_Node<K, V> *n = newElement<skiplist_Node<K, V>>(k, v, _max_level);
    return n;
}

/* 
                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+

*/
template<typename K, typename V>
int SkipList<K, V>::insert_element(const K key, const V value) {            //返回值：成功为0，节点已经存在返回1
    
    mtx.lock();

    //从fakehead开始
    skiplist_Node<K, V> *current = this->fakehead;

    //tempnode存储待插节点的前一个节点的ptr 用该数组给待插节点的forward赋值
    skiplist_Node<K, V> *tempnode[_max_level+1];
    memset(tempnode, 0, sizeof(skiplist_Node<K, V>*)*(_max_level+1));  

    // 在skiplist构造函数中，创造了一个假头结点，其forwards全初始化为NULL，并且跳表curlevel一开始为0
    for(int i = _cur_level; i >= 0; i--) {
        while(current->forward[i] != NULL && current->forward[i]->get_key() < key) {
            current = current->forward[i]; 
        }
        //while终止条件：两种情况
        //1.current下一个节点为空，表示到达本level末尾，level该下降了,在下一level继续查找,此时更新tempnode[i]为current，因为tempnode在本层前一个节点一定是current
        //2.或者是current下一个节点的值比插入值key大于等于,此时current与下一个节点之间就是新节点插入位置，现在只需要下降level，并依次更新每一层tempnode，tempnode存待插节点在本层的前一个节点，while不会再进去，直到level降到0
        tempnode[i] = current;
    }

    // current已经到达level 0，移动到下一位
    current = current->forward[0];

    if (current != NULL && current->get_key() == key) {
        std::cout << "key: " << key << ", exists" << std::endl;
        mtx.unlock();
        return 1;
    }

    if (current == NULL || current->get_key() != key ) {
        
        // 获取的random_level一定不会大于max_level,但有可能大于cur_level
        int random_level = get_random_level();

        // If random level is greater thar skip list's current level, initialize tempnode value with pointer to fakehead
        if (random_level > _cur_level) {
            for (int i = _cur_level+1; i < random_level+1; i++) {
                tempnode[i] = fakehead;
            }
            _cur_level = random_level;                  //更新链表cur_level，因为这个新插入的节点刷新了curlevel
        }

        // create new node with random level generated 
        skiplist_Node<K, V>* inserted_node = create_node(key, value, random_level);
        
        for (int i = 0; i <= random_level; i++) {
            inserted_node->forward[i] = tempnode[i]->forward[i];
            tempnode[i]->forward[i] = inserted_node;
        }
        //std::cout << "Successfully inserted key:" << key << ", value:" << value << std::endl;
        _element_count ++;
    }
    mtx.unlock();                                       //解锁
    return 0;
}

// Display skip list  逻辑：从最底层level0，头结点触发，从左向右，依次打印所有节点，然后level++，重复一遍上述动作
//可以预见到 会有很多节点的key 与 value被重复打印
template<typename K, typename V> 
void SkipList<K, V>::display_list() {

    std::cout << "\n*****Skip List*****"<<"\n"; 
    for (int i = 0; i <= _cur_level; i++) {
        skiplist_Node<K, V> *node = this->fakehead->forward[i]; 
        std::cout << "Level " << i << ": ";
        while (node != NULL) {
            std::cout << node->get_key() << ":" << node->get_value() << ";";
            node = node->forward[i];
        }
        std::cout << std::endl;
    }
    std::cout<<"display_list func exit successfully"<<std::endl;
}

// 将跳表level0遍历一遍，即遍历一遍所有节点，将其key与value写到文件中 因为level 0一定包含了所有的节点
template<typename K, typename V> 
void SkipList<K, V>::dump_file() {

    std::cout << "开始落盘" << std::endl;
    _file_writer.open(STORE_FILE);
    skiplist_Node<K, V> *node = this->fakehead->forward[0]; 

    while (node != NULL) {
        _file_writer << node->get_key() << ":" << node->get_value() << "\n";//将内容输出到指定路径文件 每一个节点输入一行
        //std::cout << node->get_key() << ":" << node->get_value() << ";\n";
        node = node->forward[0];//移动到下一位
    }

    _file_writer.flush();               //刷缓冲，将存储在缓冲区的待写入内容写入磁盘中的文件，防止丢失
    _file_writer.close();               //关闭打开的文件描述符
    std::cout<<"落盘结束"<<std::endl;
    return ;
}

// Load data from disk
template<typename K, typename V> 
void SkipList<K, V>::load_file() {

    _file_reader.open(STORE_FILE);
    std::cout << "load_file-----------------" << std::endl;
    //无论k v实际是什么类型，这里都转为string
    std::string line;
    //std::string* key = new std::string();
    std::string* key = newElement< std::string >();
    //std::string* value = new std::string();
    std::string* value = newElement< std::string >();
    while (getline(_file_reader, line)) {//读取一行到line中，当读到行尾，返回false
        get_key_value_from_string(line, key, value);//将读取到的line保存到串k与v中
        if (key->empty() || value->empty()) {//若读到文件中间的空行，不做处理
            continue;
        }
        insert_element(*key, *value);
        std::cout << "key:" << *key << "value:" << *value << std::endl;
    }
     deleteElement<std::string>(key);
     deleteElement<std::string>(value);
    //delete key;//释放两个申请的string空间
    //delete value;
    _file_reader.close();//关闭读文件描述符
}


// 返回跳表节点个数，注意不算伪头结点
template<typename K, typename V> 
int SkipList<K, V>::size() { 
    return _element_count;
}

template<typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string& str, std::string* key, std::string* value) {

    if(!is_valid_string(str)) {//该行字符串无效
        return;
    }
    *key = str.substr(0, str.find(delimiter));//substr函数左闭右开
    *value = str.substr(str.find(delimiter)+1, str.length());
}

template<typename K, typename V>//判断该行字符串是否有效 true是有效 false是无效
bool SkipList<K, V>::is_valid_string(const std::string& str) {

    if (str.empty()) {
        return false;
    }
    if (str.find(delimiter) == std::string::npos) {//std::string::npos是未找到的返回值
        return false;
    }
    return true;
}

/* 
                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+

*/
// Delete element from skip list 
template<typename K, typename V> 
void SkipList<K, V>::delete_element(K key) {

    mtx.lock();
    skiplist_Node<K, V> *current = this->fakehead; 
    skiplist_Node<K, V> *tempnode[_max_level+1];//与插入节点类似，tempnode保存待删节点各个层级的前一个节点，也是一个temp数组
    memset(tempnode, 0, sizeof(skiplist_Node<K, V>*)*(_max_level+1));

    // 和插入节点类似，从最高等级开始查
    for (int i = _cur_level; i >= 0; i--) {
        while (current->forward[i] !=NULL && current->forward[i]->get_key() < key) {
            current = current->forward[i];
        }
        //while终止条件：两种情况
        //1.current下一个节点为空，表示到达本level末尾，level该下降了,在下一level继续查找,此时更新tempnode[i]为current，因为待删节点在本层前一个节点一定是current
        //2.或者是current下一个节点的值比插入值key大于等于,此时current的下一个节点可能是待删节点，现在只需要下降level，并依次更新每一层tempnode，tempnode存待删节点在本层的前一个节点，即current，while不会再进去，直到level降到0
        tempnode[i] = current;
    }

    current = current->forward[0];
    if (current != NULL && current->get_key() == key) {//current就是待删节点
        
        //level从低到高开始做删除
        for (int i = 0; i <= _cur_level; i++) {

            // if at level i, next node is not target node, break the loop.  这意味着待删节点已经删除完全
            if (tempnode[i]->forward[i] != current) 
                break;

            tempnode[i]->forward[i] = current->forward[i];  //current前一位的后驱指针更新为current后一位
        }

        // Remove levels which have no elements 这里更新cur_level,因为有可能删除节点是当前跳表中level最高的
        while (_cur_level > 0 && fakehead->forward[_cur_level] == 0) {
            _cur_level --; 
        }

        std::cout << "Successfully deleted key "<< key << std::endl;
        //delete current;//这里会调用node析构函数
        deleteElement<skiplist_Node<K, V>>(current);
        _element_count --;//元素数量变化
    }
    mtx.unlock();
    std::cout<<"delete func exit successfully"<<std::endl;
    return;
}

template<typename K, typename V>
void SkipList<K, V>::delete_skiplist() {
    int size = 0;
    skiplist_Node<K,V> *node=this->fakehead->forward[0];//node指向伪头结点下一位

    //如果该跳表头结点下一位为空，不用删,直接返回
    if(node != NULL) {
        //断开fakehead1 与 head之间的联系
        for(int i = 0; i<_max_level+1; i++) {
            this->fakehead->forward[i] = NULL;
        }

        while(node != NULL) {
            skiplist_Node<K,V> *temp = node->forward[0];//temp先指向待删节点下一位
            //delete node;//将node删除        这里会调用node的析构
            deleteElement<skiplist_Node<K, V>>(node);
            size++;
            this->_element_count--;
            node = temp;//node更新到下一位
        }
    }
    std::cout << " 删除的节点个数为 " << size << std::endl;
    std::cout << " 当前节点个数为 " << this->_element_count << std::endl;
    return ;
}


template<typename K, typename V> 
SkipList<K, V>::~SkipList() {
    std::cout<<"调用析构函数\n";
    if (_file_writer.is_open()) {
        _file_writer.close();
    }
    if (_file_reader.is_open()) {
        _file_reader.close();
    }
    delete_skiplist();
    //delete fakehead;            //释放构造函数中为head开辟的结点空间，因为上面的函数并没有删除fakehead 
    deleteElement<skiplist_Node<K, V>> (fakehead);
}

//该值一定不会大于max_level,max_level是一开始定死的，获得的返回值为0概率0.5，为k(k<maxlevel)概率为pow(0.5,k+1),为maxlevel的概率为1-之前的概率
template<typename K, typename V>
int SkipList<K, V>::get_random_level(){

    int k = 1;
    while (rand() % 2) {
        k++;
    }
    k = (k < _max_level) ? k : _max_level;
    return k;
};

#endif