import { describe, it, expect } from 'vitest';
import { ApiError } from './types';

describe('ApiError', () => {
  it('can be thrown and caught', () => {
    expect(() => {
      throw new ApiError(404, 'Not found');
    }).toThrow('Not found');
  });

  it('is an instance of ApiError', () => {
    const err = new ApiError(500, 'Internal Server Error');
    expect(err).toBeInstanceOf(ApiError);
  });

  it('is an instance of Error (inherits from Error)', () => {
    const err = new ApiError(400, 'Bad Request');
    expect(err).toBeInstanceOf(Error);
  });

  it('exposes httpStatus property', () => {
    const err = new ApiError(403, 'Forbidden');
    expect(err.httpStatus).toBe(403);
  });

  it('exposes message property', () => {
    const err = new ApiError(404, 'Not found');
    expect(err.message).toBe('Not found');
  });

  it('has name set to "ApiError"', () => {
    const err = new ApiError(422, 'Unprocessable Entity');
    expect(err.name).toBe('ApiError');
  });

  it('httpStatus is readonly (value is preserved)', () => {
    const err = new ApiError(503, 'Service Unavailable');
    expect(err.httpStatus).toBe(503);
  });

  it('works with different HTTP status codes', () => {
    const statuses = [200, 201, 400, 401, 403, 404, 500, 503];
    for (const status of statuses) {
      const err = new ApiError(status, `Status ${status}`);
      expect(err.httpStatus).toBe(status);
      expect(err.message).toBe(`Status ${status}`);
    }
  });
});
