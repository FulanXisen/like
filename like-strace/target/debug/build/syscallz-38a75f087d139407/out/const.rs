/// The default kill action, defaults to KillProcess on supported libseccomp versions and falls back to KillThread otherwise
pub const DEFAULT_KILL: Action = Action::KillThread;