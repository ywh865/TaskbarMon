#pragma once

bool is_auto_start_task_active_for_this_user(std::wstring* path);
// The application always starts as the interactive user.  Do not create an
// elevated scheduled task from an otherwise as-invoker executable.
bool create_auto_start_task_for_this_user();
bool delete_auto_start_task_for_this_user();
