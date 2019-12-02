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

int numOfOperation;
int numOfOperation2;
//Structure for Insert operation
struct Insert
{
	//Structure for arguments passed to Insert
	struct Arguments
	{
		Node *p;//Node to be inserted
		Node *x;//p is inserted next to x
	}args;
	struct LocalVariables
	{
		Node *x_rlink_llink;//This announces the value of x->rlink->llink which needs to be set to p.
		Node **x_rlink_llink_address;//This announces the address of x->rlink->llink which is used as destination in __sync_val_compare_and_swap
		Node *x_rlink;//This announces the value of x->rlink which needs to be set to p.
		Node **x_rlink_address;//This announces the address of x->rlink which is used as destination in __sync_val_compare_and_swap
	}lv;
};

//Structure for Delete operation
struct Delete
{
	//Structure for arguments passed to Delete
	struct _Arguments
	{
		Node *x;//Node to be deleted.
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
	volatile AnnounceOp* announce;//Current announcement
	Node *first;//First node
	Node *end;//End node
}LinkedList;

LinkedList l;
void initialize(void)
{
	//Current announcement is that no operation is in progress
	l.announce=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp),GFP_KERNEL);
	l.announce->opName=NONE;

	//Create 4 node doubly linked list
	l.first=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	l.end=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	l.first->rlink=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	l.first->rlink->rlink=(Node *)kmalloc(sizeof(struct Node),GFP_KERNEL );
	l.first->llink=0;
	l.first->rlink->llink=l.first;
	l.first->rlink->rlink->rlink=l.end;
	l.first->rlink->rlink->llink=l.first->rlink;
	l.end->rlink=0;
	l.end->llink=l.first->rlink->rlink;
}

//Insert node p to the right of node x
int Insert(Node *p,Node *x)
{
	if(p==0||x==0) return 0;
	AnnounceOp *curAnnouncedOp;
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp) ,GFP_KERNEL);//To announce an insert operation.
	while(1)
	{
		curAnnouncedOp=(AnnounceOp *)l.announce;
		AnnounceOp *hp0=curAnnouncedOp;
		if(curAnnouncedOp!=l.announce) continue;
		if(curAnnouncedOp->opName==NONE)
		{

				if(l.first==x||l.end==x||l.end->llink==x)//Insertion should not be after first or after end or after one node before end
				{
					kfree(nextAnnounceOp);
					return 0;
				}
				p->llink=x;//Set p's left as x
				p->rlink=x->rlink;//Set p's right as x's right
				if(p->rlink==0||p->llink==0)  goto label;
				nextAnnounceOp->opName=INSERT;//Set INSERT as the next operation
				nextAnnounceOp->insert.args.p=p;
				nextAnnounceOp->insert.args.x=x;

				//Announce the value of x->rlink which needs to be set to p
				nextAnnounceOp->insert.lv.x_rlink=x->rlink;
				if(nextAnnounceOp->insert.lv.x_rlink==0)  goto label;//Node x is no more in the linked list
				Node *hp2=nextAnnounceOp->insert.lv.x_rlink;//Set hazard pointer
				if(x->rlink!=nextAnnounceOp->insert.lv.x_rlink)  goto label;//Check that hazard pointer has been set accurately
				nextAnnounceOp->insert.lv.x_rlink_address=&x->rlink;//Announce the address of x->rlink to be used as destination in __sync_val_compare_and_swap

				//Announce the value of x->rlink->llink which needs to be set to p
				nextAnnounceOp->insert.lv.x_rlink_llink=nextAnnounceOp->insert.lv.x_rlink->llink;
				if(nextAnnounceOp->insert.lv.x_rlink_llink==0)  goto label;//Node next to node x is unlinked
				Node *hp1=nextAnnounceOp->insert.lv.x_rlink_llink;//Set hazard pointer
				if(nextAnnounceOp->insert.lv.x_rlink->llink!=nextAnnounceOp->insert.lv.x_rlink_llink)  goto label;//Check hazard pointer is set correctly
				nextAnnounceOp->insert.lv.x_rlink_llink_address=&nextAnnounceOp->insert.lv.x_rlink->llink;//Announce the address of x->rlink->llink to be used as destination in __sync_val_compare_and_swap

				//To announce the start of insert operation.
				void *v1=(void*)(nextAnnounceOp);
				void *v2=(void*)(curAnnouncedOp);
				void *res = (void*)__sync_val_compare_and_swap((volatile long*)&l.announce, (long) v2 ,(long) v1 );
				if(res==v2)
				{
					InsertHelper(nextAnnounceOp);
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
label:
	kfree(nextAnnounceOp);
	return 0;
}

int Delete(Node *x)
{
	if(x==0) return 0;
	AnnounceOp *curAnnouncedOp;
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp) ,GFP_KERNEL);//To announce a delete operation.
	Node *replacement_x_llink=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	Node *replacement_x_rlink=(Node *)kmalloc(sizeof(struct Node) ,GFP_KERNEL);
	while(1)
	{
		curAnnouncedOp=(AnnounceOp *)l.announce;
		AnnounceOp *hp0=curAnnouncedOp;
		if(curAnnouncedOp!=l.announce) continue;
		if(curAnnouncedOp->opName==NONE)
		{
				if(l.first==x||l.end==x||l.first->rlink==x||l.end->llink==x) //Check x is not one of the four dummy nodes
				{
					kfree(nextAnnounceOp);
					kfree(replacement_x_llink);
					kfree(replacement_x_rlink);
					return 0;
				}
				//Set Delete as the next operation
				nextAnnounceOp->opName=DELET;
				nextAnnounceOp->del.args.x=x;

				nextAnnounceOp->del.lv.x_llink=x->llink;//Announce the value of x->llink to be used in CAS
				Node *hp1=nextAnnounceOp->del.lv.x_llink;//Set hazard pointer
				if(nextAnnounceOp->del.lv.x_llink!=x->llink||nextAnnounceOp->del.lv.x_llink==0) goto label1;//Check hazard pointer is set accurately and x->llink is not zero
				nextAnnounceOp->del.lv.x_llink_address=&x->llink;//Announce the address of x->llink to be used in CAS

				//Following statements are on the same pattern as above i.e. announce the value of variable
				//Set hazard pointer. check hazard pointer is set accurately. Announce the address of that variable

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

				nextAnnounceOp->del.lv.replacement_x_llink=replacement_x_llink;//Announce the replacement for the node left to x
				nextAnnounceOp->del.lv.replacement_x_rlink=replacement_x_rlink;//Announce the replacement for the node right to x
				replacement_x_llink->data=nextAnnounceOp->del.lv.x_llink->data;//Copy data
				replacement_x_rlink->data=nextAnnounceOp->del.lv.x_rlink->data;//Copy data

				replacement_x_llink->rlink=replacement_x_rlink;
				replacement_x_llink->llink=nextAnnounceOp->del.lv.x_llink_llink;
				replacement_x_rlink->llink=replacement_x_llink;
				replacement_x_rlink->rlink=nextAnnounceOp->del.lv.x_rlink_rlink;

				//To announce the start of delete operation.
				void *v1=(void*)(nextAnnounceOp);
				void *v2=(void*)(curAnnouncedOp);
				void *res = (void*)__sync_val_compare_and_swap((long*)&l.announce, (long) v2 ,(long) v1 );
				if(res==v2)
				{
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
//Set x's right link to node p (newly created node)
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->insert.lv.x_rlink_address),(long)curAnnouncedOp->insert.lv.x_rlink,(long)curAnnouncedOp->insert.args.p);
//Set the left pointer of node next to x to point to p
__sync_val_compare_and_swap((volatile long *)(curAnnouncedOp->insert.lv.x_rlink_llink_address),(long)curAnnouncedOp->insert.lv.x_rlink_llink,(long)curAnnouncedOp->insert.args.p);	
    //To announce that insert operation is complete.
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp),GFP_KERNEL );
	nextAnnounceOp->opName=NONE;
	void *v1=(void*)(nextAnnounceOp);
	void *v2=(void*)(curAnnouncedOp);
	if(__sync_val_compare_and_swap((volatile long *)(&l.announce),(long)v2,(long)v1)==(long)v2)
	{

	}
	else
	{
		kfree(nextAnnounceOp);
	}
}

//Second part of delete
void DeleteHelper(AnnounceOp *curAnnouncedOp)
{
	//Replace 2 nodes around x including x with two new nodes
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

	}
	//To announce that delete operation is complete.
	AnnounceOp *nextAnnounceOp=(AnnounceOp*)kmalloc(sizeof(struct AnnounceOp) ,GFP_KERNEL);
	nextAnnounceOp->opName=NONE;
	void *v1=(void *)(nextAnnounceOp);
	void *v2=(void *)(curAnnouncedOp);
	if(__sync_val_compare_and_swap((volatile long *)(&l.announce),(long)v2,(long)v1)==(long)v2)
	{

	}
	else
	{
		kfree(nextAnnounceOp);
	}
}
#define NUMOP 15000
void threadTest0(void){
     Node *first=l.first;
    Node *x=first->rlink;
    	while(numOfOperation > NUMOP)
	    {
		int a;
		get_random_bytes(&a, sizeof(a));
		    if(x->rlink==l.end)
			x=first->rlink;
		    else if(x->rlink!=0)
			x=x->rlink;
		    else
			x=first->rlink;

	    }
}
void threadTest1(void){
    Node *first=l.first;
    Node *x=first->rlink;
    int i;
    int insertCount=0;
    int iterations =50;
    for(i=0;NUMOP < numOfOperation;i++, numOfOperation++)
    {
	Node *p=(Node *)kmalloc(sizeof(struct Node),GFP_KERNEL);
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
}


void threadTest2(void){
    Node *first=l.first;
    Node *x=first->rlink;
    int i;
    int insertCount=0;
    int iterations=50;
    for(i=0;NUMOP < numOfOperation;i++, numOfOperation++)
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
    for(i=0;NUMOP < numOfOperation2;i++, numOfOperation2++)
    {
	struct my_node* new = kmalloc(sizeof(struct my_node), GFP_KERNEL);
        new->data = i;
	int b = 1;
	struct my_node *current_node;
	spin_lock(&spinlock);
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
	spin_unlock(&spinlock);	
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
    for(i=0;NUMOP < numOfOperation2;i++, numOfOperation2++)
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

void threadTest5(void){
    int i;
    for(i=0;NUMOP<numOfOperation2;i++)
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

	//printk("%d\n",i);
    
    }
    

    ssleep(5);
    printk("Lock-Free Linked List Insert Time: %lld ns", end - start);
    //printk("First for loop finish\n");
 
    numOfOperation = 0;
    getnstimeofday(&starttime);
    start = (unsigned long)starttime.tv_sec*1000000000 + (unsigned long)starttime.tv_nsec;
    for(i =0; i<100;i++){
	if(i == 50){
		kthread_run((void*)threadTest1, NULL, "test_thread");
		kthread_run((void*)threadTest2, NULL, "test_thread");
	}
	else
		kthread_run((void*)threadTest0,NULL,"test_thread");
	
	//printk("%d\n",i);
    
    }

    ssleep(5);
    printk("Lock-Free Linked List concurrent Time: %lld ns", end - start);
    //printk("Second for loop finish\n");
 

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

	//printk("%d\n",i);
    
    }

    ssleep(5);
    printk("Lock Linked List Insert Time: %lld ns", end - start);
    //printk("First for loop finish\n");
 
    numOfOperation2 = 0;
    getnstimeofday(&starttime);
    start = (unsigned long)starttime.tv_sec*1000000000 + (unsigned long)starttime.tv_nsec;
    for(i =0; i<100;i++){
	if(i == 50){
		kthread_run((void*)threadTest3, NULL, "test_thread");
		kthread_run((void*)threadTest4, NULL, "test_thread");
	}
	else
		kthread_run((void*)threadTest5,NULL,"test_thread");
	
    }

    ssleep(5);
    printk("Lock Linked List concurrent Time: %lld ns", end - start);
    //printk("Second for loop finish\n");
    return 0;
}


void __exit dLinkedListTest_cleanup(void){
    printk("BYE MODULE\n");
}

module_init(dLinkedListTest_init);
module_exit(dLinkedListTest_cleanup);
MODULE_LICENSE("GPL");

