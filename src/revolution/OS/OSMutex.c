#include <revolution/OS.h>

void OSInitMutex(OSMutex* mutex) {
    OSInitThreadQueue(&mutex->queue);
    mutex->thread = NULL;
    mutex->lock = 0;
}

void OSLockMutex(OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currThread = OSGetCurrentThread();

    while (TRUE) {
        if (mutex->thread == NULL) {
            OSMutex* tail;

            mutex->thread = currThread;
            mutex->lock++;

            tail = currThread->queueMutex.tail;
            if (tail == NULL) {
                currThread->queueMutex.head = mutex;
            } else {
                tail->next = mutex;
            }

            mutex->prev = tail;
            mutex->next = NULL;
            currThread->queueMutex.tail = mutex;
            break;
        } else if (mutex->thread == currThread) {
            mutex->lock++;
            break;
        } else {
            currThread->mutex = mutex;
            __OSPromoteThread(mutex->thread, currThread->priority);
            OSSleepThread(&mutex->queue);
            currThread->mutex = NULL;
        }
    }

    OSRestoreInterrupts(enabled);
}

void OSUnlockMutex(OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currThread = OSGetCurrentThread();

    if (mutex->thread == currThread) {
        mutex->lock--;

        if (mutex->lock == 0) {
            OSMutex* next = mutex->next;
            OSMutex* prev = mutex->prev;

            if (next == NULL) {
                currThread->queueMutex.tail = prev;
            } else {
                next->prev = prev;
            }

            if (prev == NULL) {
                currThread->queueMutex.head = next;
            } else {
                prev->next = next;
            }

            mutex->thread = NULL;
            if (currThread->priority < currThread->base) {
                currThread->priority = __OSGetEffectivePriority(currThread);
            }

            OSWakeupThread(&mutex->queue);
        }
    }

    OSRestoreInterrupts(enabled);
}

void __OSUnlockAllMutex(OSThread* thread) {
    OSMutex* mutex;
    while ((mutex = thread->queueMutex.head) != NULL) {
        OSMutex* next = mutex->next;
        if (next == NULL) {
            thread->queueMutex.tail = NULL;
        } else {
            next->prev = NULL;
        }

        thread->queueMutex.head = next;
        mutex->lock = 0;
        mutex->thread = NULL;
        OSWakeupThread(&mutex->queue);
    }
};

BOOL OSTryLockMutex(OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currThread = OSGetCurrentThread();
    BOOL lock = FALSE;

    if (mutex->thread == NULL) {
        OSMutex* tail;

        mutex->thread = currThread;
        mutex->lock++;

        tail = currThread->queueMutex.tail;
        if (tail == NULL) {
            currThread->queueMutex.head = mutex;
        } else {
            tail->next = mutex;
        }

        mutex->prev = tail;
        mutex->next = NULL;

        currThread->queueMutex.tail = mutex;
        lock = TRUE;
    } else if (mutex->thread == currThread) {
        mutex->lock++;
        lock = TRUE;
    } else {
        lock = FALSE;
    }

    OSRestoreInterrupts(enabled);
    return lock;
}

void OSInitCond(OSCond* cond)
{
	OSInitThreadQueue(&cond->queue);
}

#define RemoveItemMutex(queue, mutex) \
	do {                                    \
		OSMutex* next;                      \
		OSMutex* prev;                      \
                                            \
		next = (mutex)->next;               \
		prev = (mutex)->prev;               \
                                            \
		if (next == NULL)                   \
			(queue)->tail = prev;           \
		else                                \
			next->prev = prev;              \
                                            \
		if (prev == NULL)                   \
			(queue)->head = next;           \
		else                                \
			prev->next = next;              \
	} while (0)

void OSWaitCond(OSCond* cond, OSMutex* mutex)
{
	BOOL enabled = OSDisableInterrupts();
	OSThread* currentThread = OSGetCurrentThread();
	s32 count;

	if (mutex->thread == currentThread) {
		count = mutex->lock;
		mutex->lock = 0;
		RemoveItemMutex(&currentThread->queueMutex, mutex);
		mutex->thread = NULL;

		if (currentThread->priority < currentThread->base) {
			currentThread->priority = __OSGetEffectivePriority(currentThread);
		}

		OSDisableScheduler();
		OSWakeupThread(&mutex->queue);
		OSEnableScheduler();
		OSSleepThread(&cond->queue);
		OSLockMutex(mutex);
		mutex->lock = count;
	}

	OSRestoreInterrupts(enabled);
}

void OSSignalCond(OSCond* cond)
{
	OSWakeupThread(&cond->queue);
}

BOOL __OSCheckMutex(OSMutex* mutex)
{
	OSThread* thread;
	OSThreadQueue* queue;
	s32 priority;

	priority = 0;
	queue    = &mutex->queue;

	if (queue->head != NULL && queue->head->link.prev != NULL) {
		return 0;
	}
	if (queue->tail != NULL && queue->tail->link.next != NULL) {
		return 0;
	}
	thread = queue->head;
	while (thread) {
		if (thread->link.next != NULL && (thread != thread->link.next->link.prev)) {
			return 0;
		}
		if (thread->link.prev != NULL && (thread != thread->link.prev->link.next)) {
			return 0;
		}
		if (thread->state != 4) {
			return 0;
		}
		if (thread->priority < priority) {
			return 0;
		}
		priority = thread->priority;
		thread   = thread->link.next;
	}
	if (mutex->thread) {
		if (mutex->lock <= 0) {
			return 0;
		}
	} else if (mutex->lock != 0) {
		return 0;
	}
	return 1;
}

BOOL __OSCheckDeadLock(OSThread* thread)
{
	OSMutex* mutex = thread->mutex;

	while (mutex && mutex->thread) {
		if (mutex->thread == thread) {
			return 1;
		}
		mutex = mutex->thread->mutex;
	}
	return 0;
}

BOOL __OSCheckMutexes(OSThread* thread)
{
	OSMutex* mutex = thread->queueMutex.head;

	while (mutex) {
		if (mutex->thread != thread) {
			return 0;
		}
		if (__OSCheckMutex(mutex) == 0) {
			return 0;
		}
		mutex = mutex->next;
	}
	return 1;
}
