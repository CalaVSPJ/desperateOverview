#ifndef DESPERATEOVERVIEW_CORE_IPC_H
#define DESPERATEOVERVIEW_CORE_IPC_H

typedef void (*DesperateOverviewCoreRefreshHook)(void *user_data);

int  desperateOverview_core_ipc_init(void);
void desperateOverview_core_ipc_shutdown(void);
int  desperateOverview_core_ipc_send_command(const char *command);

int  desperateOverview_core_ipc_start_events(DesperateOverviewCoreRefreshHook hook,
                                             void *user_data);
void desperateOverview_core_ipc_stop_events(void);

#endif /* DESPERATEOVERVIEW_CORE_IPC_H */

