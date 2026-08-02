import re


def workflow_job(workflow, job_id):
    match = re.search(
        rf"(?ms)^  {re.escape(job_id)}:\s*\n(?P<body>.*?)(?=^  [A-Za-z0-9_-]+:\s*$|\Z)",
        workflow,
    )
    if match is None:
        raise AssertionError(f"Missing workflow job: {job_id}")
    return match.group("body")
