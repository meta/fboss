/**
 * (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
 */

import React from 'react';
import Layout from '@theme/Layout';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import styles from './changelog.module.css';

export default function ChangelogPage(props) {
  const {siteConfig} = useDocusaurusContext();

  // When using modules in Docusaurus routes, the data is imported and accessible via props
  // The changelog module contains the actual data array
  const changelogData = props.changelog?.default || props.changelog || [];
  const changelog = Array.isArray(changelogData) ? changelogData : [];

  return (
    <Layout
      title="Changelog"
      description="FBOSS Documentation Changelog - Recent updates and releases">
      <main className="container margin-vert--lg">
        <div className="row">
          <div className="col col--8 col--offset-2">
            <h1>Changelog</h1>
            <p className="margin-bottom--lg">
              Stay up to date with the latest changes and improvements to FBOSS Documentation.
            </p>

            {changelog.length === 0 ? (
              <p>No changelog entries yet.</p>
            ) : (
              <div className={styles.changelogList}>
                {changelog.map((entry) => (
                  <article key={entry.id} className={styles.changelogEntry}>
                    <header>
                      <h2 className={styles.changelogTitle}>
                        {entry.title}
                        {entry.version && (
                          <span className={styles.changelogVersion}>
                            {entry.version}
                          </span>
                        )}
                      </h2>
                      {entry.date && (
                        <time className={styles.changelogDate}>
                          {new Date(entry.date).toLocaleDateString('en-US', {
                            year: 'numeric',
                            month: 'long',
                            day: 'numeric',
                          })}
                        </time>
                      )}
                    </header>
                    <div className={styles.changelogContent}>
                      {entry.commits ? (
                        <>
                          <p className={styles.commitCount}>
                            {entry.commits.length} commit{entry.commits.length > 1 ? 's' : ''} affecting documentation
                          </p>
                          <ul className={styles.commitList}>
                            {entry.commits.map((commit, idx) => (
                              <li key={idx} className={styles.commitItem}>
                                <div className={styles.commitSubject}>
                                  {commit.subject}
                                </div>
                                <div className={styles.commitMeta}>
                                  <a
                                    href={`https://github.com/meta/fboss/commit/${commit.hash}`}
                                    target="_blank"
                                    rel="noopener noreferrer"
                                    className={styles.commitHashLink}
                                  >
                                    <code className={styles.commitHash}>
                                      {commit.hash.substring(0, 8)}
                                    </code>
                                  </a>
                                  <span className={styles.commitDate}>{commit.date}</span>
                                </div>
                              </li>
                            ))}
                          </ul>
                        </>
                      ) : (
                        <div>{entry.content}</div>
                      )}
                    </div>
                  </article>
                ))}
              </div>
            )}
          </div>
        </div>
      </main>
    </Layout>
  );
}
