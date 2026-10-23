@extends('layouts.app')

@section('content')
    <h1>Welcome, {{ $name }}</h1>
    @foreach ($projects as $project)
        <article>{{ $project }}</article>
    @endforeach
@endsection
