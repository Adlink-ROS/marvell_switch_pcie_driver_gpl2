/*
 *
 * If you received this File from Marvell, you may opt to use, redistribute and/or
 * modify this File in accordance with the terms and conditions of the General
 * Public License Version 2, June 1991 (the "GPL License"), a copy of which is
 * available along with the File in the license.txt file or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or
 * on the worldwide web at http://www.gnu.org/licenses/gpl.txt.
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY
 * DISCLAIMED. The GPL License provides additional details about this warranty
 * disclaimer.
 *
 */

#ifdef CONFIG_PM

#include "oak_dpm.h"
#include "oak_net.h"

/* Name        : oak_dpm_set_power_state
 * Returns     : void
 * Parameters  : struct device *dev, pci_power_t state
 * Description : This function set the device power state
 */
void oak_dpm_set_power_state(struct device *dev, pci_power_t state)
{
	int retval;
	pci_power_t D0 = 0;
	struct net_device *ndev = dev_get_drvdata(dev);

	oak_t *np = netdev_priv(ndev);

	/* If user input state is D0 and current_state not D0
	 * i.e. current state is D3hot (D1, D2, D3) then
	 * we call resume operation
	 */
	if (state == PCI_D0 && np->pdev->current_state != D0) {
		retval = oak_dpm_resume(dev);
		if (retval)
			pr_info("oak_dpm_resume: failed.\n");
	}
	/* If user input state is D1 or D2 or D3 and current_state D0
	 * then we do the suspend operation
	 */
	else if (((state == PCI_D1) ||
		  (state == PCI_D2) ||
		  (state == PCI_D3hot)) && (np->pdev->current_state == D0)) {
		retval = oak_dpm_suspend(dev);
		if (retval != 0)
			pr_info("oak_dpm_suspend: failed\n");
	}
}

/* Name        : oak_dpm_state_store
 * Returns     : ssize_t
 * Parameters  : struct device *dev, struct device_attribute *attr,
 *               const char *buf, size_t count
 * Description : This function store the sysfs entry
 */
static ssize_t oak_dpm_state_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	pci_power_t pwr;
	bool d0, d1, d2, d3;

	/* Validate user input */
	d0 = sysfs_streq(buf, "D0");
	d1 = sysfs_streq(buf, "D1");
	d2 = sysfs_streq(buf, "D2");
	d3 = sysfs_streq(buf, "D3");

	/* D0 as input, set device as D0
	 * D1, D2, and D3 as input, set the device as D3
	 * For any other input, error message is triggered.
	 */
	if (d0) {
		pwr = PCI_D0;
		oak_dpm_set_power_state(dev, pwr);
	} else if (d1 || d2 || d3) {
		pwr = PCI_D3hot;
		oak_dpm_set_power_state(dev, pwr);
	} else {
		pr_err("oak: Wrong input, Device power states are D0, D1, D2 or D3\n");
	}

	/* With the current sysfs implementation the kobject reference count is
	 * only modified directly by the function sysfs_schedule_callback().
	 */
	return count;
}

/* The sysfs kernel object device attribute file oak_dpm_state
 * is wrtite only. Hence only oak_dpm_state_store function is
 * called by kernel when a user does input to oak_dpm_state file
 */
static DEVICE_ATTR_WO(oak_dpm_state);

/* Name        : oak_dpm_create_sysfs
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function creates sysfs entry for setting device power
 *               states D0, D1, D2 and D3.
 */
void oak_dpm_create_sysfs(oak_t *np)
{
	int status;

	status = sysfs_create_file(&np->pdev->dev.kobj,
				   &dev_attr_oak_dpm_state.attr);
	if (status)
		pr_err("oak: Failed to create sysfs entry\n");
}

/* Name        : oak_dpm_remove_sysfs
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function removes sysfs entry of device power states
 */
void oak_dpm_remove_sysfs(oak_t *np)
{
	sysfs_remove_file(&np->pdev->dev.kobj, &dev_attr_oak_dpm_state.attr);
}

#ifdef CONFIG_PM_SLEEP

/* Name        : oak_dpm_suspend
 * Returns     : int
 * Parameters  : struct device *dev
 * Description : This function is called when system goes into suspend state
 *               and put the device into sleep state
 */
/* polyspace-begin CODE-METRICS:* [Justified] "Deviation HDRV-1441" */
int __maybe_unused oak_dpm_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	oak_t *np = netdev_priv(ndev);
	int retval = 0;

	/* If interface is up then, do driver specific operations
	 * gracefully close the interface
	 */
	retval = netif_running(ndev);
	if (retval)
		oak_net_close(ndev);

	/* Inform to PCI core, wake me up from D3hot when event triggers */
	pci_enable_wake(np->pdev, PCI_D3hot, true);

	/* Release the oak hardware */
	oak_release_hardware(np->pdev);

	/* To synchronize changes hold rtnl lock */
	rtnl_lock();

	/* Save the PCI state and prepare to go for sleep */
	pci_save_state(np->pdev);
	pci_prepare_to_sleep(np->pdev);

	/* Set the device power state as D3hot */
	retval = pci_set_power_state(np->pdev, PCI_D3hot);
	if (retval == 0)
		pr_info("%s: dpm state=D%d\n", __func__,
			np->pdev->current_state);
	else
		pr_err("%s: Failed to set the device power state err: %d\n",
		       __func__, retval);

	/* Release the rtnl lock */
	rtnl_unlock();

	return retval;
}
/* polyspace-end CODE-METRICS:* [Justified] */

/* Name        : oak_dpm_resume
 * Returns     : int
 * Parameters  : struct device *dev
 * Description : This function called when system goes into resume state and put
 *               the device into active state
 */
/* polyspace-begin CODE-METRICS:* [Justified] "Deviation HDRV-1441" */
int __maybe_unused oak_dpm_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	oak_t *np = netdev_priv(ndev);
	int retval = 0;

	/* To synchronize changes hold rtnl lock */
	rtnl_lock();

	/* Set the device power state as D0 */
	retval = pci_set_power_state(np->pdev, PCI_D0);
	if (retval == 0)
		pr_info("%s: dpm state=D%d\n", __func__,
			np->pdev->current_state);

	/* Restore the PCI state */
	pci_restore_state(np->pdev);

	/* The driver specific operations
	 * Initialize the oak hardware
	 * If interface is up, then call oak net open
	 */
	retval = oak_init_hardware(np->pdev);
	if (retval) {
		pr_err("%s: oak init hardware Not Successful %d\n", __func__,
		       retval);
	} else {
		retval = netif_running(ndev);
		if (retval) {
			retval = oak_net_open(ndev);
			if (retval)
				pr_err("%s: oak net open failed\n", __func__);
		}
	}

	/* Release the rtnl lock */
	rtnl_unlock();

	return retval;
}
/* polyspace-end CODE-METRICS:* [Justified] */
#endif
#endif
