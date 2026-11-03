'use strict';

const fs = require('fs');

const PROJECTS_PATH = '.github/pr-projects.json';
const PROJECT_LABEL_PREFIX = 'project:';
const PROJECT_LABEL_COLOR = '1d76db';

async function removeLabel(github, params) {
  try {
    await github.rest.issues.removeLabel(params);
  } catch (error) {
    if (error.status !== 404) {
      throw error;
    }
  }
}

async function createLabel(github, params) {
  try {
    await github.rest.issues.createLabel(params);
  } catch (error) {
    if (error.status !== 422) {
      throw error;
    }
  }
}

module.exports = async function labelPullRequestMetadata({
  github,
  context,
  core,
}) {
  const {owner, repo} = context.repo;
  const issueNumber = context.payload.pull_request.number;
  const {data: pullRequest} = await github.rest.pulls.get({
    owner,
    repo,
    pull_number: issueNumber,
  });
  const validCompanies = [
    'accton',
    'celestica',
    'nexthop',
    'arista',
    'cisco',
    'nvidia',
    'meta',
  ];
  const {npiProjects, nonNpiProjects} = JSON.parse(
    fs.readFileSync(PROJECTS_PATH, 'utf8'),
  );
  const projects = [...npiProjects, ...nonNpiProjects];
  const validProjects = new Set(projects.map(project => project.toLowerCase()));
  const currentLabels = await github.paginate(
    github.rest.issues.listLabelsOnIssue,
    {owner, repo, issue_number: issueNumber, per_page: 100},
  );
  const isManagedLabel = name => {
    const normalizedName = name.toLowerCase();
    return (
      validCompanies.includes(normalizedName) ||
      normalizedName.startsWith(PROJECT_LABEL_PREFIX)
    );
  };

  async function reconcileManagedLabels(desiredLabels) {
    const managedLabels = currentLabels.filter(({name}) =>
      isManagedLabel(name),
    );
    for (const {name} of managedLabels) {
      if (!desiredLabels.has(name.toLowerCase())) {
        await removeLabel(github, {
          owner,
          repo,
          issue_number: issueNumber,
          name,
        });
      }
    }
  }

  const match = pullRequest.title.match(/^\[([^\]]+)\]\s*\[([^\]]+)\]\s+.+/);
  if (!match) {
    await reconcileManagedLabels(new Set());
    core.warning(
      'PR title must use the format "[CompanyName][ProjectName] Your PR title", ' +
        'for example "[Accton][minipack3n] Add transceiver support".',
    );
    return;
  }

  const company = match[1].trim().toLowerCase();
  const project = match[2].trim().toLowerCase();
  if (!validCompanies.includes(company)) {
    await reconcileManagedLabels(new Set());
    core.warning(`Company name "${match[1].trim()}" is not recognized.`);
    return;
  }
  if (!validProjects.has(project)) {
    await reconcileManagedLabels(new Set([company]));
    await github.rest.issues.addLabels({
      owner,
      repo,
      issue_number: issueNumber,
      labels: [company],
    });
    core.warning(
      `Project name "${match[2].trim()}" is not recognized. ` +
        `Supported projects: ${projects.join(', ')}.`,
    );
    return;
  }

  const projectLabel = `${PROJECT_LABEL_PREFIX}${project}`;
  const repositoryLabels = await github.paginate(
    github.rest.issues.listLabelsForRepo,
    {owner, repo, per_page: 100},
  );
  if (!repositoryLabels.some(({name}) => name.toLowerCase() === projectLabel)) {
    await createLabel(github, {
      owner,
      repo,
      name: projectLabel,
      color: PROJECT_LABEL_COLOR,
      description: `FBOSS project: ${project}`,
    });
  }

  await reconcileManagedLabels(new Set([company, projectLabel]));

  await github.rest.issues.addLabels({
    owner,
    repo,
    issue_number: issueNumber,
    labels: [company, projectLabel],
  });
  core.info(`Added labels "${company}" and "${projectLabel}".`);
};
