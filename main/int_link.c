#include "int_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

// 创建一个新节点
Node *create_node(int value)
{
	Node *node = (Node *)malloc(sizeof(Node));
	node->value = value;
	node->next = node; // 默认循环指向自己
	node->prev = node;
	return node;
}

// 插入节点（按升序插入）
void insert_node_sorted(Node **head, int value)
{
	Node *new_node = create_node(value);

	// 如果链表为空
	if (*head == NULL) {
		*head = new_node;
		return;
	}

	Node *current = *head;

	// 找到插入位置
	while (current->next != *head && current->next->value < value) {
		current = current->next;
	}

	// 插入节点
	new_node->next = current->next;
	new_node->prev = current;
	current->next->prev = new_node;
	current->next = new_node;

	// 如果新节点的值小于头节点的值，则更新头节点
	if (value < (*head)->value) {
		*head = new_node;
	}
}

// 打印链表（调试用）
void print_list(Node *head)
{
	if (head == NULL) {
		printf("List is empty.\n");
		return;
	}

	Node *current = head;
	do {
		printf("%d ", current->value);
		current = current->next;
	} while (current != head);
	printf("\n");
}

// 找到链表中的最大值
int find_max_in_list(Node *head)
{
	if (head == NULL) {
		return -1; // 如果链表为空，返回 -1
	}

	int max_value = head->value;
	Node *current = head->next;
	while (current != head) {
		if (current->value > max_value) {
			max_value = current->value;
		}
		current = current->next;
	}
	return max_value;
}

// 释放链表
void free_list(Node *head) {
    if (head == NULL) return;

    Node *current = head;
    do {
        Node *next = current->next;
        free(current);
        current = next;
    } while (current != head);
}