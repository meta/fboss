'use strict';

const assert = require('assert').strict;
const labelPullRequestMetadata = require('./label-pr-metadata');

async function runCase(title, labels) {
  const removed = [];
  const added = [];
  const created = [];
  const warnings = [];
  const listLabelsOnIssue = () => {};
  const listLabelsForRepo = () => {};
  const github = {
    rest: {
      pulls: {get: async () => ({data: {title}})},
      issues: {
        listLabelsOnIssue,
        listLabelsForRepo,
        removeLabel: async ({name}) => removed.push(name),
        addLabels: async ({labels: names}) => added.push(names),
        createLabel: async ({name}) => created.push(name),
      },
    },
    paginate: async endpoint =>
      endpoint === listLabelsOnIssue ? labels.map(name => ({name})) : [],
  };
  const context = {
    repo: {owner: 'meta', repo: 'fboss'},
    payload: {pull_request: {number: 1828}},
  };
  const core = {warning: message => warnings.push(message), info: () => {}};

  await labelPullRequestMetadata({github, context, core});
  return {removed, added, created, warnings};
}

async function main() {
  const unknownProject = await runCase(
    '[Nexthop][m4340nhp] Add platform',
    ['nexthop', 'accton', 'project:misc', 'bug'],
  );
  assert.deepEqual(unknownProject.removed, ['accton', 'project:misc']);
  assert.deepEqual(unknownProject.added, [['nexthop']]);
  assert.deepEqual(unknownProject.created, []);
  assert.match(unknownProject.warnings[0], /m4340nhp.*not recognized/);

  const missingCompanyLabel = await runCase(
    '[Nexthop][m4340nhp] Add platform',
    [],
  );
  assert.deepEqual(missingCompanyLabel.added, [['nexthop']]);

  const invalidCompany = await runCase(
    '[Unknown][misc] Add platform',
    ['nexthop', 'project:misc'],
  );
  assert.deepEqual(invalidCompany.removed, ['nexthop', 'project:misc']);
  assert.deepEqual(invalidCompany.added, []);

  const malformedTitle = await runCase('Add platform', [
    'nexthop',
    'project:misc',
  ]);
  assert.deepEqual(malformedTitle.removed, ['nexthop', 'project:misc']);
  assert.deepEqual(malformedTitle.added, []);

  const validProject = await runCase(
    '[Nexthop][misc] Add platform',
    ['accton', 'project:feature'],
  );
  assert.deepEqual(validProject.removed, ['accton', 'project:feature']);
  assert.deepEqual(validProject.added, [['nexthop', 'project:misc']]);
  assert.deepEqual(validProject.created, ['project:misc']);
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
