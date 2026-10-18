# Startup observations

User-authorized capability: replace the misleading "Not frozen" assertion.
No logs for more than fifteen seconds can occur during CPU work, graphics or
asset loading; background-service logs can continue after a failed app launch.
Neither proves success, failure or a hang. Show elapsed time and observed log
activity separately from window/frame milestones. State that launch is not yet
confirmed. Never fail solely on a timeout or ordinary Wine diagnostic.

Explicit runtime failures, aborted boot, and high-confidence loader failures
stop indefinite loading and provide a visible error plus logs/retry. Avoid
equating the initial Wine Unix PID's exit with Windows process death (Wine may
handoff). Test silence, repeated/background noise, fatal loader errors and a
successful delayed frame; preserve normal slow Celeste startup.
