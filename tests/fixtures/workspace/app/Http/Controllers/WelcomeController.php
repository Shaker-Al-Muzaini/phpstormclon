<?php

namespace App\Http\Controllers;

use Illuminate\Http\Request;
use Illuminate\View\View;

class WelcomeController extends Controller
{
    /** Show the project workspace. */
    public function index(Request $request): View
    {
        $projects = [
            'Forge IDE',
            'Laravel API',
            'Native developer tools',
        ];

        return view('welcome', [
            'name' => 'Shaker',
            'projects' => $projects,
        ]);
    }
}
