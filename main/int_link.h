#ifndef __INT_LINK_H__
#define __INT_LINK_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

// 链表节点结构
typedef struct Node {
	int value;
	struct Node *next;
	struct Node *prev;
} Node;

// 创建一个新节点
Node *create_node(int value);

// 插入节点（按升序插入）
void insert_node_sorted(Node **head, int value);

// 打印链表（调试用）
void print_list(Node *head);

// 找到链表中的最大值
int find_max_in_list(Node *head);

void free_list(Node *head);

#endif