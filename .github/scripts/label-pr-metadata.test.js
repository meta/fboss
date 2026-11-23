'use strict';

const assert = require('assert').strict;
const labelPullRequestMetadata = require('./label-pr-metadata');

async function runCase(title, labels, comments = []) {
  const removed = [];
  const added = [];
  const created = [];
  const warnings = [];
  const createdComments = [];
  const updatedComments = [];
  const deletedComments = [];
  const listLabelsOnIssue = () => {};
  const listLabelsForRepo = () => {};
  const listComments = () => {};
  const github = {
    rest: {
      pulls: {get: async () => ({data: {title, user: {login: 'contributor'}}})},
      issues: {
        listLabelsOnIssue,
        listLabelsForRepo,
        listComments,
        removeLabel: async ({name}) => removed.push(name),
        addLabels: async ({labels: names}) => added.push(names),
        createLabel: async ({name}) => created.push(name),
        createComment: async params => createdComments.push(params),
        updateComment: async params => updatedComments.push(params),
        deleteComment: async params => deletedComments.push(params),
      },
    },
    paginate: async endpoint => {
      if (endpoint === listLabelsOnIssue) {
        return labels.map(name => ({name}));
      }
      return endpoint === listComments ? comments : [];
    },
  };
  const context = {
    repo: {owner: 'meta', repo: 'fboss'},
    payload: {pull_request: {number: 1828}},
  };
  const core = {warning: message => warnings.push(message), info: () => {}};

  await labelPullRequestMetadata({github, context, core});
  return {
    removed,
    added,
    created,
    warnings,
    createdComments,
    updatedComments,
    deletedComments,
  };
}

async function main() {
  const unknownProject = await runCase(
    '[Nexthop][not-a-supported-project] Add platform',
    ['nexthop', 'accton', 'project:misc', 'bug'],
  );
  assert.deepEqual(unknownProject.removed, ['accton', 'project:misc']);
  assert.deepEqual(unknownProject.added, [['nexthop']]);
  assert.deepEqual(unknownProject.created, []);
  assert.match(
    unknownProject.warnings[0],
    /not-a-supported-project.*not recognized/,
  );
  assert.equal(unknownProject.createdComments.length, 1);
  assert.match(
    unknownProject.createdComments[0].body,
    /project name.*not recognized/,
  );

  const missingCompanyLabel = await runCase(
    '[Nexthop][not-a-supported-project] Add platform',
    [],
  );
  assert.deepEqual(missingCompanyLabel.added, [['nexthop']]);

  const invalidCompany = await runCase(
    '[Unknown][misc] Add platform',
    ['nexthop', 'project:misc'],
  );
  assert.deepEqual(invalidCompany.removed, ['nexthop', 'project:misc']);
  assert.deepEqual(invalidCompany.added, []);
  assert.equal(invalidCompany.createdComments.length, 1);
  assert.match(
    invalidCompany.createdComments[0].body,
    /company name.*not recognized/,
  );

  const malformedTitle = await runCase('Add platform', [
    'nexthop',
    'project:misc',
  ]);
  assert.deepEqual(malformedTitle.removed, ['nexthop', 'project:misc']);
  assert.deepEqual(malformedTitle.added, []);
  assert.equal(malformedTitle.createdComments.length, 1);
  const warning = malformedTitle.createdComments[0];
  assert.equal(warning.owner, 'meta');
  assert.equal(warning.repo, 'fboss');
  assert.equal(warning.issue_number, 1828);
  assert.match(warning.body, /@contributor/);
  assert.match(warning.body, /Please edit the title of your PR/);
  assert.ok(warning.body.includes('[CompanyName][ProjectName] Your PR title'));
  assert.ok(warning.body.includes('[Accton][minipack3n] Add transceiver support'));
  assert.ok(
    warning.body.includes(
      '[supported companies and projects]' +
        '(https://github.com/meta/fboss/blob/main/.github/pr-projects.json)',
    ),
  );
  assert.doesNotMatch(warning.body, /Supported (companies|projects):/);

  const existingWarning = {
    id: 123,
    body: warning.body,
    user: {login: 'github-actions[bot]'},
  };
  const repeatedInvalidTitle = await runCase('Add platform', [], [
    existingWarning,
  ]);
  assert.deepEqual(repeatedInvalidTitle.createdComments, []);
  assert.deepEqual(repeatedInvalidTitle.updatedComments, []);
  assert.deepEqual(repeatedInvalidTitle.deletedComments, []);

  const changedProblem = await runCase(
    '[Nexthop][not-a-supported-project] Add platform',
    [],
    [existingWarning],
  );
  assert.deepEqual(changedProblem.createdComments, []);
  assert.equal(changedProblem.updatedComments.length, 1);
  assert.equal(
    changedProblem.updatedComments[0].comment_id,
    existingWarning.id,
  );
  assert.match(
    changedProblem.updatedComments[0].body,
    /project name.*not recognized/,
  );

  const userComment = {...existingWarning, id: 456, user: {login: 'contributor'}};
  const userMarker = await runCase('Add platform', [], [userComment]);
  assert.equal(userMarker.createdComments.length, 1);
  assert.deepEqual(userMarker.updatedComments, []);
  assert.deepEqual(userMarker.deletedComments, []);

  const validProject = await runCase(
    '[Nexthop][misc] Add platform',
    ['accton', 'project:feature'],
  );
  assert.deepEqual(validProject.removed, ['accton', 'project:feature']);
  assert.deepEqual(validProject.added, [['nexthop', 'project:misc']]);
  assert.deepEqual(validProject.created, ['project:misc']);
  assert.deepEqual(validProject.createdComments, []);
  assert.deepEqual(validProject.updatedComments, []);
  assert.deepEqual(validProject.deletedComments, []);

  const correctedTitle = await runCase(
    '[Nexthop][misc] Add platform',
    [],
    [userComment, existingWarning],
  );
  assert.deepEqual(correctedTitle.added, [['nexthop', 'project:misc']]);
  assert.deepEqual(correctedTitle.createdComments, []);
  assert.deepEqual(correctedTitle.updatedComments, []);
  assert.deepEqual(correctedTitle.deletedComments, [
    {owner: 'meta', repo: 'fboss', comment_id: existingWarning.id},
  ]);
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
