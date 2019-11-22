#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/random.h>
#include <linux/list.h>
#include <linux/spinlock.h>

spinlock_t spinlock;
struct timespec starttime;
struct timespec endtime;
unsigned long long start;
unsigned long long end;
struct my_node{
    struct list_head list;
    int data;
};
struct list_head my_list;

typedef struct Node
{
    int data;
    struct Node *rlink;
    struct Node *llink;
}Node;

//Following is the structure for Insert operation
struct Insert
{
	//Following is the structure for arguments passed to Insert
	struct Arguments
	{
		Node *p;//node to be inserted
		Node *x;//p is inserted next to x
	}args;
	struct LocalVariables
	{
		Node *x_rlink_llink;//This announces the value of x->rlink->llink which needs to be set to p.
		Node **x_rlink_llink_address;//This announces the address of x->rlink->llink which is used as destination in InterlockedCompareExchange.
		Node *x_rlink;//This announces the value of x->rlink which needs to be set to p.
		Node **x_rlink_address;//This announces the address of x->rlink which is used as destination in InterlockedCompareExchange.
	}lv;
};

//Following is the structure for Delete operation
struct Delete
{
	//Following is the structure for arguments passed to Delete
	struct _Arguments
	{
		Node *x;//node to be deleted.
	}args;
	struct _LocalVariables
	{
		//Following variables announce the values and addresses of pointers found in nodes around x (including x as well).
		Node *x_llink;
		Node **x_llink_address;
		Node *x_rlink;
		Node **x_rlink_address;
		Node *x_llink_rlink;
		Node **x_llink_rlink_address;
		Node *x_rlink_llink;
		Node **x_rlink_llink_address;
		Node *x_llink_llink;
		Node **x_llink_llink_address;
		Node *x_rlink_rlink;
		Node **x_rlink_rlink_address;
		Node *x_llink_llink_rlink;
		Node **x_llink_llink_rlink_address;
		Node *x_rlink_rlink_llink;
		Node **x_rlink_rlink_llink_address;

		Node *replacement_x_llink;//This points to the replacement for the node left to x.
		Node *replacement_x_rlink;//This points to the replacement for the node right to x.
	}lv;
};

enum OperationName{NONE=0,INSERT=1,DELET=2};
//Following structure contains the operations defined earlier.
typedef struct AnnounceOp
{
	enum OperationName opName;
	union
	{
		struct Insert insert;
		struct Delete del;
	};
}AnnounceOp;
void DeleteHelper(AnnounceOp *curAnnouncedOp);
void InsertHelper(AnnounceOp *curAnnouncedOp);

typedef struct LinkedList
{
	volatile AnnounceOp* announce;//current announcement
	Node *first;//first node
	Node *end;//end node
}LinkedList;

LinkedList l;
void initialize(void)
{
	//current announcement is that no operation is in progress
	l.announce=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp),GFP_KERNEL);//new AnnounceOp;
	//assert(l.announce);
	l.announce->opName=NONE;

	//create 4 node doubly linked list
	l.first=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);//new Node;
	//assert(l.first);
	l.end=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	//assert(l.end);
	l.first->rlink=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	//assert(l.first->rlink);
	l.first->rlink->rlink=(Node *)kmalloc(sizeof(struct Node),GFP_KERNEL );
	//assert(l.first->rlink->rlink);
	l.first->llink=0;
	l.first->rlink->llink=l.first;
	l.first->rlink->rlink->rlink=l.end;
	l.first->rlink->rlink->llink=l.first->rlink;
	l.end->rlink=0;
	l.end->llink=l.first->rlink->rlink;
}

//insert node p to the right of node x
int Insert(Node *p,Node *x)
{
	if(p==0||x==0) return 0;
	AnnounceOp *curAnnouncedOp;
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp) ,GFP_KERNEL);//To announce an insert operation.
	//assert(nextAnnounceOp);
	while(1)
	{
		curAnnouncedOp=(AnnounceOp *)l.announce;
		AnnounceOp *hp0=curAnnouncedOp;
		if(curAnnouncedOp!=l.announce) continue;
		if(curAnnouncedOp->opName==NONE)
		{

				if(l.first==x||l.end==x||l.end->llink==x)//insertion should not be after first or after end or after one node before end
				{
					kfree(nextAnnounceOp);
					return 0;
				}
				p->llink=x;//set p's left as x
				p->rlink=x->rlink;//set p's right as x's right
				if(p->rlink==0||p->llink==0)  goto label;
				nextAnnounceOp->opName=INSERT;//set INSERT as the next operation
				nextAnnounceOp->insert.args.p=p;
				nextAnnounceOp->insert.args.x=x;

				//announce the value of x->rlink which needs to be set to p
				nextAnnounceOp->insert.lv.x_rlink=x->rlink;
				if(nextAnnounceOp->insert.lv.x_rlink==0)  goto label;//node x is no more in the linked list
				Node *hp2=nextAnnounceOp->insert.lv.x_rlink;//set hazard pointer
				if(x->rlink!=nextAnnounceOp->insert.lv.x_rlink)  goto label;//check that hazard pointer has been set accurately
				nextAnnounceOp->insert.lv.x_rlink_address=&x->rlink;//announce the address of x->rlink to be used as destination in InterlockedCompareExchange

				//announce the value of x->rlink->llink which needs to be set to p
				nextAnnounceOp->insert.lv.x_rlink_llink=nextAnnounceOp->insert.lv.x_rlink->llink;
				if(nextAnnounceOp->insert.lv.x_rlink_llink==0)  goto label;//node next to node x is unlinked
				Node *hp1=nextAnnounceOp->insert.lv.x_rlink_llink;//set hazard pointer
				if(nextAnnounceOp->insert.lv.x_rlink->llink!=nextAnnounceOp->insert.lv.x_rlink_llink)  goto label;//check hazard pointer is set correctly
				nextAnnounceOp->insert.lv.x_rlink_llink_address=&nextAnnounceOp->insert.lv.x_rlink->llink;//announce the address of x->rlink->llink to be used as destination in InterlockedCompareExchange.



				//Check that announced addresses has not changed
				/*if(&x->rlink->llink!=nextAnnounceOp->insert.lv.x_rlink_llink_address)  goto label;
				if(&x->rlink!=nextAnnounceOp->insert.lv.x_rlink_address)  goto label;*/

				//To announce the start of insert operation.
				void *v1=(void*)(nextAnnounceOp);
				void *v2=(void*)(curAnnouncedOp);
				void *res = (void*)__sync_val_compare_and_swap((volatile long*)&l.announce, (long) v2 ,(long) v1 );
				//void *res=(void *)__atomic_compare_exchange((volatile long)(&l.announce),(long)v1,(long)v2);
				if(res==v2)
				{
					//RetireNode(curAnnouncedOp);
					InsertHelper(nextAnnounceOp);
					return 1;
				}
		
		}
		else if(curAnnouncedOp->opName==INSERT)
		{
		    printk("INSERTHELPER\n");
		    InsertHelper(curAnnouncedOp);
		}
		else if(curAnnouncedOp->opName==DELET)
		{
			DeleteHelper(curAnnouncedOp);
		}
	}
label:
	kfree(nextAnnounceOp);
	return 0;
}

int Delete(Node *x)
{
	if(x==0) return 0;
	AnnounceOp *curAnnouncedOp;
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp) ,GFP_KERNEL);//new AnnounceOp;//To announce a delete operation.
	//assert(nextAnnounceOp);
	Node *replacement_x_llink=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);//new Node;
	//assert(replacement_x_llink);
	Node *replacement_x_rlink=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);//new Node;
	//assert(replacement_x_rlink);
	while(1)
	{
		curAnnouncedOp=(AnnounceOp *)l.announce;
		AnnounceOp *hp0=curAnnouncedOp;
		if(curAnnouncedOp!=l.announce) continue;
		if(curAnnouncedOp->opName==NONE)
		{
				if(l.first==x||l.end==x||l.first->rlink==x||l.end->llink==x) //check x is not one of the four dummy nodes
				{
					kfree(nextAnnounceOp);
					kfree(replacement_x_llink);
					kfree(replacement_x_rlink);
					return 0;
				}
				//set Delete as the next operation
				nextAnnounceOp->opName=DELET;
				nextAnnounceOp->del.args.x=x;

				nextAnnounceOp->del.lv.x_llink=x->llink;//announce the value of x->llink to be used in CAS
				Node *hp1=nextAnnounceOp->del.lv.x_llink;//set hazard pointer
				if(nextAnnounceOp->del.lv.x_llink!=x->llink||nextAnnounceOp->del.lv.x_llink==0) goto label1;//check hazard pointer is set accurately and x->llink is not zero
				nextAnnounceOp->del.lv.x_llink_address=&x->llink;//announce the address of x->llink to be used in CAS

				//Following statements are on the same pattern as above i.e. announce the value of variable
				//set hazard pointer. check hazard pointer is set accurately. Announce the address of that variable

				nextAnnounceOp->del.lv.x_rlink=x->rlink;
				Node *hp2=nextAnnounceOp->del.lv.x_rlink;
				if(nextAnnounceOp->del.lv.x_rlink!=x->rlink||nextAnnounceOp->del.lv.x_rlink==0)  goto label1;
				nextAnnounceOp->del.lv.x_rlink_address=&x->rlink;

				nextAnnounceOp->del.lv.x_llink_rlink=nextAnnounceOp->del.lv.x_llink->rlink;
				Node *hp3=nextAnnounceOp->del.lv.x_llink_rlink;
				if(nextAnnounceOp->del.lv.x_llink_rlink!=nextAnnounceOp->del.lv.x_llink->rlink||nextAnnounceOp->del.lv.x_llink_rlink==0)  goto label1;
				nextAnnounceOp->del.lv.x_llink_rlink_address=&nextAnnounceOp->del.lv.x_llink->rlink;

				nextAnnounceOp->del.lv.x_rlink_llink=nextAnnounceOp->del.lv.x_rlink->llink;
				Node *hp4=nextAnnounceOp->del.lv.x_rlink_llink;
				if(nextAnnounceOp->del.lv.x_rlink_llink!=nextAnnounceOp->del.lv.x_rlink->llink||nextAnnounceOp->del.lv.x_rlink_llink==0)  goto label1;
				nextAnnounceOp->del.lv.x_rlink_llink_address=&nextAnnounceOp->del.lv.x_rlink->llink;

				nextAnnounceOp->del.lv.x_rlink_rlink=nextAnnounceOp->del.lv.x_rlink->rlink;
				Node *hp5=nextAnnounceOp->del.lv.x_rlink_rlink;
				if(nextAnnounceOp->del.lv.x_rlink_rlink!=nextAnnounceOp->del.lv.x_rlink->rlink||nextAnnounceOp->del.lv.x_rlink_rlink==0)  goto label1;
				nextAnnounceOp->del.lv.x_rlink_rlink_address=&nextAnnounceOp->del.lv.x_rlink->rlink;

				nextAnnounceOp->del.lv.x_llink_llink=nextAnnounceOp->del.lv.x_llink->llink;
				Node *hp6=nextAnnounceOp->del.lv.x_llink_llink;
				if(nextAnnounceOp->del.lv.x_llink_llink!=nextAnnounceOp->del.lv.x_llink->llink||nextAnnounceOp->del.lv.x_llink_llink==0)  goto label1;
				nextAnnounceOp->del.lv.x_llink_llink_address=&nextAnnounceOp->del.lv.x_llink->llink;

				nextAnnounceOp->del.lv.x_llink_llink_rlink=nextAnnounceOp->del.lv.x_llink_llink->rlink;
				Node *hp7=nextAnnounceOp->del.lv.x_llink_llink_rlink;
				if(nextAnnounceOp->del.lv.x_llink_llink_rlink!=nextAnnounceOp->del.lv.x_llink_llink->rlink||nextAnnounceOp->del.lv.x_llink_llink_rlink==0)  goto label1;
				nextAnnounceOp->del.lv.x_llink_llink_rlink_address=&nextAnnounceOp->del.lv.x_llink_llink->rlink;

				nextAnnounceOp->del.lv.x_rlink_rlink_llink=nextAnnounceOp->del.lv.x_rlink_rlink->llink;
				Node *hp8=nextAnnounceOp->del.lv.x_rlink_rlink_llink;
				if(nextAnnounceOp->del.lv.x_rlink_rlink_llink!=nextAnnounceOp->del.lv.x_rlink_rlink->llink||nextAnnounceOp->del.lv.x_rlink_rlink_llink==0)  goto label1;
				nextAnnounceOp->del.lv.x_rlink_rlink_llink_address=&nextAnnounceOp->del.lv.x_rlink_rlink->llink;

				nextAnnounceOp->del.lv.replacement_x_llink=replacement_x_llink;//announce the replacement for the node left to x
				nextAnnounceOp->del.lv.replacement_x_rlink=replacement_x_rlink;//announce the replacement for the node right to x
				replacement_x_llink->data=nextAnnounceOp->del.lv.x_llink->data;//copy data
				replacement_x_rlink->data=nextAnnounceOp->del.lv.x_rlink->data;//copy data
				//build the chain
	//x_llink_llink//replacement_x_llink//replacement_x_rlink//x_rlink_rlink
	//	---------	-------             --------               -------
	//	|		|	|	   |-----------|        |              |      |
	//	|		|===|      |===========|        |==============|      |
	//	 -------	-------             --------                -------
				replacement_x_llink->rlink=replacement_x_rlink;
				replacement_x_llink->llink=nextAnnounceOp->del.lv.x_llink_llink;
				replacement_x_rlink->llink=replacement_x_llink;
				replacement_x_rlink->rlink=nextAnnounceOp->del.lv.x_rlink_rlink;//x->rlink->rlink;

				//check addresses has not changed
				/*if(nextAnnounceOp->del.lv.x_llink_address!=&x->llink) goto label1;
				if(nextAnnounceOp->del.lv.x_rlink_address!=&x->rlink)  goto label1;
				if(nextAnnounceOp->del.lv.x_llink_rlink_address!=&x->llink->rlink)  goto label1;
				if(nextAnnounceOp->del.lv.x_rlink_llink_address!=&x->rlink->llink)  goto label1;
				if(nextAnnounceOp->del.lv.x_rlink_rlink_address!=&x->rlink->rlink)  goto label1;
				if(nextAnnounceOp->del.lv.x_llink_llink_address!=&x->llink->llink)  goto label1;
				if(nextAnnounceOp->del.lv.x_llink_llink_rlink_address!=&x->llink->llink->rlink)  goto label1;
				if(nextAnnounceOp->del.lv.x_rlink_rlink_llink_address!=&x->rlink->rlink->llink)  goto label1;*/

				//To announce the start of delete operation.
				void *v1=(void*)(nextAnnounceOp);
				void *v2=(void*)(curAnnouncedOp);
				void *res = (void*)__sync_val_compare_and_swap((long*)&l.announce, (long) v2 ,(long) v1 );
				if(res==v2)
				{
					//RetireNode(curAnnouncedOp);
					DeleteHelper(nextAnnounceOp);
					return 1;
				}
		}
		else if(curAnnouncedOp->opName==INSERT)
		{
			InsertHelper(curAnnouncedOp);
		}
		else if(curAnnouncedOp->opName==DELET)
		{
			DeleteHelper(curAnnouncedOp);
		}
	}
label1:
	kfree(nextAnnounceOp);
	kfree(replacement_x_llink);
	kfree(replacement_x_rlink);
	return 0;
}

//Second part of insert
void InsertHelper(AnnounceOp *curAnnouncedOp)
{
//set x's right link to node p (newly created node)
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->insert.lv.x_rlink_address),(long)curAnnouncedOp->insert.lv.x_rlink,(long)curAnnouncedOp->insert.args.p);
//set the left pointer of node next to x to point to p
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->insert.lv.x_rlink_llink_address),(long)curAnnouncedOp->insert.lv.x_rlink_llink,(long)curAnnouncedOp->insert.args.p);	
    //To announce that insert operation is complete.
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp),GFP_KERNEL );
	//assert(nextAnnounceOp);
	nextAnnounceOp->opName=NONE;
	void *v1=(void*)(nextAnnounceOp);
	void *v2=(void*)(curAnnouncedOp);
	if(__sync_val_compare_and_swap((volatile long *)(&l.announce),(long)v2,(long)v1)==(long)v2)
	{
		//RetireNode(v2);
	}
	else
	{
		kfree(nextAnnounceOp);
	}
}

//Second part of delete
void DeleteHelper(AnnounceOp *curAnnouncedOp)
{
	//replace 2 nodes around x including x with two new nodes
	__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_llink_llink_rlink_address),(long)curAnnouncedOp->del.lv.x_llink_llink_rlink,(long)curAnnouncedOp->del.lv.replacement_x_llink);
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_rlink_rlink_llink_address),(long)curAnnouncedOp->del.lv.x_rlink_rlink_llink,(long)curAnnouncedOp->del.lv.replacement_x_rlink);
	//Set 3 retired nodes pointer fields to 0
	__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_llink_llink_address),(long)curAnnouncedOp->del.lv.x_llink_llink,(long)0);
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_llink_rlink_address),(long)curAnnouncedOp->del.lv.x_llink_rlink,(long)0);
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_rlink_rlink_address),(long)curAnnouncedOp->del.lv.x_rlink_rlink,(long)0);
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_rlink_llink_address),(long)curAnnouncedOp->del.lv.x_rlink_llink,(long)0);
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_llink_address),(long)curAnnouncedOp->del.lv.x_llink,(long)0);
	if(__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->del.lv.x_rlink_address),(long)curAnnouncedOp->del.lv.x_rlink,(long)0)==(long)curAnnouncedOp->del.lv.x_rlink)
	{
		//RetireNode(x);
		//RetireNode(curAnnouncedOp->del.lv.x_llink);
		//RetireNode(curAnnouncedOp->del.lv.x_rlink);
	}
	//To announce that delete operation is complete.
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp) ,GFP_KERNEL);
	//assert(nextAnnounceOp);
	nextAnnounceOp->opName=NONE;
	void *v1=(void *)(nextAnnounceOp);
	void *v2=(void *)(curAnnouncedOp);
	if(__sync_val_compare_and_swap((volatile long *)(&l.announce),(long)v2,(long)v1)==(long)v2)
	{
		//RetireNode(curAnnouncedOp);
	}
	else
	{
		kfree(nextAnnounceOp);
	}
}

void threadTest1(void){
    Node *first=l.first;
    Node *x=first->rlink;
    int i;
    int insertCount=0;
    int iterations =50;
    for(i=0;i<iterations;i++)
    {
	Node *p=(Node *)kmalloc(sizeof(struct Node),GFP_KERNEL);//new Node;
	//assert(p);
	p->data=i;
	do
	{
	    insertCount++;
	    x=first->rlink;
	    while(1)
	    {
		int a;
		get_random_bytes(&a, sizeof(a));
		if(a%2==0)
		    break;
		else
		{
		    if(x->rlink==l.end)
			x=first->rlink;
		    else if(x->rlink!=0)
			x=x->rlink;
		    else
			x=first->rlink;
		}

	    }

	}while(!Insert(p,x));
    }
    getnstimeofday(&endtime);
    end = (unsigned long)endtime.tv_sec*1000000000 + (unsigned long)endtime.tv_nsec;
    printk("TestFinished\n");
}


void threadTest2(void){
    Node *first=l.first;
    Node *x=first->rlink;
    int i;
    int insertCount=0;
    int iterations=50;
    for(i=0;i<iterations;i++)
    {
	do
	{
	    x=l.first->rlink->rlink;
	    while(1)
	    {
		int a;
		get_random_bytes(&a, sizeof(a));
		if(a%2==0)
		    break;
		else
		{
		    if(x->rlink==l.end||x->rlink==l.end->llink)
			x=l.first->rlink->rlink;
		    else if(x->rlink!=0)
			x=x->rlink;
		    else
			x=l.first->rlink->rlink;
		}

	    }
	}
	while(!Delete(x));
    }
	getnstimeofday(&endtime);
    	end = (unsigned long)endtime.tv_sec*1000000000 + (unsigned long)endtime.tv_nsec;
}

void threadTest3(void)
{
    
    int i;
    int insertCount=0;
    int iterations=50;
    for(i=0;i<iterations;i++)
    {
	struct my_node* new = kmalloc(sizeof(struct my_node), GFP_KERNEL);
        new->data = i;
	int b = 1;
	struct my_node *current_node;
	while(b)
	{
		struct list_head *p;
		list_for_each(p, &my_list)
		{
		    int a;
		    get_random_bytes(&a, sizeof(a));
		    if(a%2==0){
		        current_node = list_entry(p, struct my_node, list);
		        b = 0;
			break;
		    }
		}

	}
	list_add(&new->list, &current_node->list);
    }
	getnstimeofday(&endtime);
    	end = (unsigned long)endtime.tv_sec*1000000000 + (unsigned long)endtime.tv_nsec;
}

void threadTest4(void)
{
    Node *first=l.first;
    Node *x=first->rlink;
    int i;
    int insertCount=0;
    int iterations=50;
    for(i=0;i<iterations;i++)
    {
	spin_lock(&spinlock);
	int b = 1;
	struct my_node *current_node;
	    while(b)
	    {
		struct list_head *p;
		list_for_each(p, &my_list)
		{
		    int a;
		    get_random_bytes(&a, sizeof(a));
		    if(a%2==0){
		        current_node = list_entry(p, struct my_node, list);
		        b = 0;
			break;
		    }
		}

	    }
        list_del(&current_node->list);
        kfree(current_node);
	spin_unlock(&spinlock);	
    }
    getnstimeofday(&endtime);
    end = (unsigned long)endtime.tv_sec*1000000000 + (unsigned long)endtime.tv_nsec;
}
    

int __init dLinkedListTest_init(void){

    printk("HELLO MODULE\n");
    initialize();
    const int THREAD_COUNT=16;
    int iterations =50;
    Node *iter = l.first->rlink->rlink;
    int count =0;
    int i = 0;

    /*lockfree list test */

    getnstimeofday(&starttime);
    start = (unsigned long)starttime.tv_sec*1000000000 + (unsigned long)starttime.tv_nsec;
    for(i =0; i<THREAD_COUNT;i++){
	kthread_run((void*)threadTest1,NULL,"test_thread");

	printk("%d\n",i);
    
    }

    ssleep(2);// 스레드 싱크 맞추기 (여기에 세마포어 걸어두시면 될것같아여
    printk("Insert Time: %lld ns", end - start);
    printk("First for loop finish\n");
 
    iter=l.first->rlink->rlink;
    count =0;

    while(iter)
    {
	iter=iter->rlink;
	count++;
    }
    count=count-2;
    printk("inserted: %d\n",count);

    getnstimeofday(&starttime);
    start = (unsigned long)starttime.tv_sec*1000000000 + (unsigned long)starttime.tv_nsec;
    for(i =0; i<THREAD_COUNT;i++){
	kthread_run((void*)threadTest2,NULL,"test_thread");
	
	printk("%d\n",i);
    
    }

    ssleep(2);// 스레드 싱크 맞추기 (여기에 세마포어 걸어두시면 될것같아여
    printk("Delete  Time: %lld ns", end - start);
    printk("Second for loop finish\n");
 
 
    iter=l.first->rlink->rlink;
    count =0;

    while(iter)
    {
	iter=iter->rlink;
	count++;
    }
    
    count=count-2;
    printk("deleted: %d\n",count);


    /* lock list test */
    INIT_LIST_HEAD(&my_list);

    // Add first list to traverse to find node to delete or node for appending. 
    struct my_node* initial_list = kmalloc(sizeof(struct my_node), GFP_KERNEL);
    initial_list->data = 1;
    list_add(&initial_list->list, &my_list);
    
    
    getnstimeofday(&starttime);
    start = (unsigned long)starttime.tv_sec*1000000000 + (unsigned long)starttime.tv_nsec;
    for(i =0; i<THREAD_COUNT;i++){
	kthread_run((void*)threadTest3,NULL,"test_thread");

	printk("%d\n",i);
    
    }

    ssleep(2);// 스레드 싱크 맞추기 (여기에 세마포어 걸어두시면 될것같아여
    printk("List Insert Time: %lld ns", end - start);
    printk("First for loop finish\n");
 

    getnstimeofday(&starttime);
    start = (unsigned long)starttime.tv_sec*1000000000 + (unsigned long)starttime.tv_nsec;
    for(i =0; i<THREAD_COUNT;i++){
	kthread_run((void*)threadTest4,NULL,"test_thread");
	
	printk("%d\n",i);
    
    }

    ssleep(2);// 스레드 싱크 맞추기 (여기에 세마포어 걸어두시면 될것같아여
    printk("List Delete  Time: %lld ns", end - start);
    printk("Second for loop finish\n");



    return 0;
}


void __exit dLinkedListTest_cleanup(void){
    printk("BYE MODULE\n");
}

module_init(dLinkedListTest_init);
module_exit(dLinkedListTest_cleanup);
MODULE_LICENSE("GPL");

