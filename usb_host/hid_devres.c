#include <linux/device.h>
#include <linux/slab.h>

/*
 * Firmware-only devres glue. In the current linked call graph, driver work is
 * flushed or cancelled before lifecycle teardown, replacing Linux's devres
 * lock while preserving linked callers' LIFO and release semantics.
 */

struct devres_node {
	struct list_head list;
	void (*release)(struct device *dev, struct devres_node *node);
	void (*free_node)(struct devres_node *node);
};

struct devres {
	struct devres_node node;
	dr_release_t release;
	unsigned long long data[];
};

struct devres_group {
	struct devres_node node[2];
	void *id;
};

struct devres_action {
	devm_action_fn action;
	void *data;
};

static void hid_devres_data_release(struct device *dev,
				    struct devres_node *node)
{
	struct devres *res = container_of(node, struct devres, node);

	if (res->release)
		res->release(dev, res->data);
}

static void hid_devres_data_free(struct devres_node *node)
{
	struct devres *res = container_of(node, struct devres, node);

	vPortFree(res);
}

static void hid_devres_group_release(struct device *dev,
				     struct devres_node *node)
{
	(void)dev;
	(void)node;
}

static void hid_devres_group_free(struct devres_node *node)
{
	struct devres_group *group =
		container_of(node, struct devres_group, node[0]);

	vPortFree(group);
}

static void hid_devres_release_node(struct device *dev,
				    struct devres_node *node)
{
	if (node->release)
		node->release(dev, node);
	if (node->free_node)
		node->free_node(node);
}

static int hid_devres_release_list(struct device *dev, struct list_head *list)
{
	int count = 0;

	while (!list_empty(list)) {
		struct devres_node *node =
			list_entry(list->prev, struct devres_node, list);
		bool resource = node->release == hid_devres_data_release;

		list_del(&node->list);
		hid_devres_release_node(dev, node);
		if (resource)
			count++;
	}

	return count;
}

static void hid_devres_detach(struct list_head *first,
			      struct list_head *last,
			      struct list_head *dest)
{
	struct list_head *before = first->prev;
	struct list_head *after = last->next;

	before->next = after;
	after->prev = before;

	dest->next = first;
	first->prev = dest;
	dest->prev = last;
	last->next = dest;
}

static struct devres_group *hid_devres_find_group(struct device *dev,
						  void *id,
						  bool open_only)
{
	struct list_head *entry;
	struct devres_node *node;

	for (entry = dev->devres.prev; entry != &dev->devres;
	     entry = entry->prev) {
		struct devres_group *group;

		node = list_entry(entry, struct devres_node, list);
		if (node->release != hid_devres_group_release)
			continue;

		group = container_of(node, struct devres_group, node[0]);
		if (id && group->id != id)
			continue;
		if ((open_only || !id) && !list_empty(&group->node[1].list))
			continue;
		return group;
	}

	return NULL;
}

static void *hid_devres_alloc(dr_release_t release, size_t size, bool zero)
{
	struct devres *res = pvPortMalloc(sizeof(*res) + size);

	if (!res)
		return NULL;

	INIT_LIST_HEAD(&res->node.list);
	res->node.release = hid_devres_data_release;
	res->node.free_node = hid_devres_data_free;
	res->release = release;
	if (zero)
		memset(res->data, 0, size);
	return res->data;
}

void *devres_alloc(dr_release_t release, size_t size, gfp_t flags)
{
	(void)flags;
	return hid_devres_alloc(release, size, true);
}

void devres_free(void *res)
{
	struct devres *devres;

	if (!res)
		return;

	devres = container_of(res, struct devres, data);
	hid_devres_data_free(&devres->node);
}

void devres_add(struct device *dev, void *res)
{
	struct devres *devres = container_of(res, struct devres, data);

	list_add_tail(&devres->node.list, &dev->devres);
}

int devres_destroy(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	struct list_head *entry;

	for (entry = dev->devres.prev; entry != &dev->devres;
	     entry = entry->prev) {
		struct devres_node *node =
			list_entry(entry, struct devres_node, list);
		struct devres *res;

		if (node->release != hid_devres_data_release)
			continue;

		res = container_of(node, struct devres, node);
		if (res->release != release)
			continue;
		if (match && !match(dev, res->data, match_data))
			continue;

		list_del(&node->list);
		hid_devres_data_free(node);
		return 0;
	}

	return -ENOENT;
}

void *devres_open_group(struct device *dev, void *id, gfp_t flags)
{
	struct devres_group *group;

	(void)flags;
	group = pvPortMalloc(sizeof(*group));
	if (!group)
		return NULL;

	INIT_LIST_HEAD(&group->node[0].list);
	group->node[0].release = hid_devres_group_release;
	group->node[0].free_node = hid_devres_group_free;
	INIT_LIST_HEAD(&group->node[1].list);
	group->node[1].release = NULL;
	group->node[1].free_node = NULL;
	group->id = id ? id : group;
	list_add_tail(&group->node[0].list, &dev->devres);
	return group->id;
}

void devres_close_group(struct device *dev, void *id)
{
	struct devres_group *group = hid_devres_find_group(dev, id, true);

	if (group)
		list_add_tail(&group->node[1].list, &dev->devres);
}

int devres_release_group(struct device *dev, void *id)
{
	struct devres_group *group = hid_devres_find_group(dev, id, false);
	struct list_head release;
	struct list_head *last;

	if (!group)
		return 0;

	INIT_LIST_HEAD(&release);
	last = list_empty(&group->node[1].list) ?
		dev->devres.prev : &group->node[1].list;
	hid_devres_detach(&group->node[0].list, last, &release);
	return hid_devres_release_list(dev, &release);
}

int devres_release_all(struct device *dev)
{
	struct list_head release;

	if (list_empty(&dev->devres))
		return 0;

	INIT_LIST_HEAD(&release);
	hid_devres_detach(dev->devres.next, dev->devres.prev, &release);
	return hid_devres_release_list(dev, &release);
}

static void hid_devres_action_release(struct device *dev, void *res)
{
	struct devres_action *action = res;

	(void)dev;
	action->action(action->data);
}

int devm_add_action_or_reset(struct device *dev, devm_action_fn action, void *data)
{
	struct devres_action *res =
		devres_alloc(hid_devres_action_release, sizeof(*res), GFP_KERNEL);

	if (!res) {
		action(data);
		return -ENOMEM;
	}

	res->action = action;
	res->data = data;
	devres_add(dev, res);
	return 0;
}

void devm_release_action(struct device *dev, devm_action_fn action, void *data)
{
	struct list_head *entry;

	for (entry = dev->devres.prev; entry != &dev->devres;
	     entry = entry->prev) {
		struct devres_node *node =
			list_entry(entry, struct devres_node, list);
		struct devres *devres;
		struct devres_action *res;

		if (node->release != hid_devres_data_release)
			continue;
		devres = container_of(node, struct devres, node);
		if (devres->release != hid_devres_action_release)
			continue;
		res = (struct devres_action *)devres->data;
		if (res->action != action || res->data != data)
			continue;

		list_del(&node->list);
		hid_devres_release_node(dev, node);
		return;
	}

	/*
	 * Pinned Linux remove_action() reaches if (WARN_ON(IS_ERR(dr))) for this
	 * same missing-action case; the reduced lookup has no ERR_PTR object.
	 */
	WARN_ON(1);
}

void *devm_kmalloc(struct device *dev, size_t size, gfp_t flags)
{
	void *res;

	(void)flags;
	res = hid_devres_alloc(NULL, size, false);
	if (res)
		devres_add(dev, res);
	return res;
}

void *devm_kzalloc(struct device *dev, size_t size, gfp_t flags)
{
	void *res;

	(void)flags;
	res = hid_devres_alloc(NULL, size, true);
	if (res)
		devres_add(dev, res);
	return res;
}

void *devm_kcalloc(struct device *dev, size_t n, size_t size, gfp_t flags)
{
	/* PORTING DEBT: callers must bound n * size until overflow is checked. */
	return devm_kzalloc(dev, n * size, flags);
}

void *devm_kmemdup(struct device *dev, const void *src, size_t size, gfp_t flags)
{
	void *dst = devm_kmalloc(dev, size, flags);

	if (dst)
		memcpy(dst, src, size);
	return dst;
}

void devm_kfree(struct device *dev, const void *ptr)
{
	struct devres *res = container_of(ptr, struct devres, data);

	(void)dev;
	list_del(&res->node.list);
	hid_devres_data_free(&res->node);
}
