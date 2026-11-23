import React from 'react';

export default function TestCase({id, children}) {
  const parts = id.split('.');
  const testName = parts[1];

  // Create the link to the test file with search query
  let link = null;
  let tooltipText = `Test: ${id}`;

  if (testName) {
    const searchQuery = `"${testName}"`;
    link = `https://github.com/meta/fboss/search?q=${encodeURIComponent(searchQuery)}`;
    tooltipText = `Test: ${id} - Click to view source`;
  }

  return (
    <div className="test-case-container">
      {link ? (
        <a
          href={link}
          className="test-badge"
          title={tooltipText}
          target="_blank"
          rel="noopener noreferrer"
        >
          <span className="test-icon">T</span>
          <span className="test-name">{id}</span>
        </a>
      ) : (
        <span className="test-badge" title={tooltipText}>
          <span className="test-icon">T</span>
          <span className="test-name">{id}</span>
        </span>
      )}
      <span className="test-content">
        {children}
      </span>
    </div>
  );
}
