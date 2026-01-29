git merge v3.34.0 --no-commit --no-ff

git rm -rf tracer
git rm -rf profiler/src/Demos/
git rm -rf shared/src/Datadog.Trace.ClrProfiler.Native
git rm -rf shared/test
git rm -rf .azure-pipelines
git rm -rf .gitlab
git rm -rf docs
git rm -rf profiler/test
git rm -rf profiler/src/Tools

git status --porcelain | grep '^DU ' | cut -c4- | xargs git rm -f

git status --porcelain | grep '^A ' | grep .github | cut -c4- | xargs git rm -f

[//]: # (todo fix cmake, make sure it creates a project)
